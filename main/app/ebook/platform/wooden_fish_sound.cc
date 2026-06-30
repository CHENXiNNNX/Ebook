#include "platform/wooden_fish_sound.hpp"

#include <cstdio>
#include <cstring>

#include <esp_heap_caps.h>

#include "core/log.hpp"
#include "data/system_state.hpp"
#include "platform/audio_codec.hpp"
#include "platform/music_player.hpp"
#include "system/task/task.hpp"

static const char* const TAG = "FishSound";

namespace app::ebook::platform {

namespace {

constexpr const char* kKnockPath   = "/assets/audio/woden_fish.wav";
constexpr uint32_t    kDstRate     = 16000;
constexpr size_t      kPlayStack   = 4096;
constexpr size_t      kWriteChunk  = 512;
constexpr uint32_t    kCapsPsram   = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
constexpr uint32_t    kOutputOffPadMs = 25;

struct WavInfo
{
    uint32_t    rate{0};
    uint8_t     channels{0};
    uint16_t    bits{0};
    const void* data{nullptr};
    size_t      data_bytes{0};
};

bool read_u32_le(const uint8_t* p, uint32_t* out)
{
    if (p == nullptr || out == nullptr) return false;
    *out = static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
    return true;
}

bool read_u16_le(const uint8_t* p, uint16_t* out)
{
    if (p == nullptr || out == nullptr) return false;
    *out = static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
    return true;
}

bool parse_wav_pcm(const uint8_t* buf, size_t len, WavInfo* info)
{
    if (buf == nullptr || info == nullptr || len < 44) return false;
    if (std::memcmp(buf, "RIFF", 4) != 0 || std::memcmp(buf + 8, "WAVE", 4) != 0)
        return false;

    size_t pos = 12;
    bool   got_fmt  = false;
    bool   got_data = false;

    while (pos + 8 <= len)
    {
        const uint8_t* ch = buf + pos;
        uint32_t       chunk_sz = 0;
        if (!read_u32_le(ch + 4, &chunk_sz)) return false;
        pos += 8;
        if (pos + chunk_sz > len) return false;

        if (std::memcmp(ch, "fmt ", 4) == 0 && chunk_sz >= 16)
        {
            uint16_t audio_fmt = 0;
            uint16_t channels  = 0;
            if (!read_u16_le(buf + pos, &audio_fmt) || audio_fmt != 1) return false;
            if (!read_u16_le(buf + pos + 2, &channels)) return false;
            info->channels = static_cast<uint8_t>(channels);
            if (!read_u32_le(buf + pos + 4, &info->rate)) return false;
            if (!read_u16_le(buf + pos + 14, &info->bits)) return false;
            if (info->bits != 16 || info->channels == 0 || info->rate == 0) return false;
            got_fmt = true;
        }
        else if (std::memcmp(ch, "data", 4) == 0)
        {
            info->data       = buf + pos;
            info->data_bytes = chunk_sz;
            got_data         = true;
        }

        pos += chunk_sz + (chunk_sz & 1U);
        if (got_fmt && got_data) return true;
    }
    return false;
}

int16_t* mono_from_interleaved(const int16_t* src, size_t frame_count, uint8_t channels,
                               size_t* out_count)
{
    if (src == nullptr || frame_count == 0 || channels == 0 || out_count == nullptr)
        return nullptr;

    *out_count = frame_count;
    if (channels == 1)
    {
        auto* dst = static_cast<int16_t*>(heap_caps_malloc(frame_count * sizeof(int16_t), kCapsPsram));
        if (dst == nullptr) return nullptr;
        std::memcpy(dst, src, frame_count * sizeof(int16_t));
        return dst;
    }

    auto* dst = static_cast<int16_t*>(heap_caps_malloc(frame_count * sizeof(int16_t), kCapsPsram));
    if (dst == nullptr) return nullptr;

    for (size_t i = 0; i < frame_count; ++i)
    {
        int32_t sum = 0;
        for (uint8_t ch = 0; ch < channels; ++ch)
            sum += src[i * channels + ch];
        dst[i] = static_cast<int16_t>(sum / static_cast<int32_t>(channels));
    }
    return dst;
}

int16_t* resample_linear(const int16_t* src, size_t src_count, uint32_t src_rate,
                         uint32_t dst_rate, size_t* out_count)
{
    if (src == nullptr || src_count == 0 || src_rate == 0 || dst_rate == 0 ||
        out_count == nullptr)
        return nullptr;

    if (src_rate == dst_rate)
    {
        auto* dst = static_cast<int16_t*>(heap_caps_malloc(src_count * sizeof(int16_t), kCapsPsram));
        if (dst == nullptr) return nullptr;
        std::memcpy(dst, src, src_count * sizeof(int16_t));
        *out_count = src_count;
        return dst;
    }

    const size_t dst_count =
        static_cast<size_t>((static_cast<uint64_t>(src_count) * dst_rate) / src_rate);
    if (dst_count == 0) return nullptr;

    auto* dst = static_cast<int16_t*>(heap_caps_malloc(dst_count * sizeof(int16_t), kCapsPsram));
    if (dst == nullptr) return nullptr;

    for (size_t i = 0; i < dst_count; ++i)
    {
        const float pos  = static_cast<float>(i) * static_cast<float>(src_rate) /
                           static_cast<float>(dst_rate);
        const size_t idx = static_cast<size_t>(pos);
        const float  frac = pos - static_cast<float>(idx);
        const int16_t a  = src[idx];
        const int16_t b  = src[(idx + 1 < src_count) ? (idx + 1) : idx];
        dst[i]           = static_cast<int16_t>(static_cast<float>(a) +
                                      frac * static_cast<float>(b - a));
    }

    *out_count = dst_count;
    return dst;
}

bool load_knock_pcm(int16_t** out_pcm, size_t* out_count, uint32_t* out_rate)
{
    if (out_pcm == nullptr || out_count == nullptr || out_rate == nullptr) return false;

    FILE* fp = std::fopen(kKnockPath, "rb");
    if (fp == nullptr)
    {
        EBOOK_LOGE(TAG, "fopen fail %s", kKnockPath);
        return false;
    }

    if (std::fseek(fp, 0, SEEK_END) != 0)
    {
        std::fclose(fp);
        return false;
    }
    const long file_sz = std::ftell(fp);
    if (file_sz <= 0)
    {
        std::fclose(fp);
        return false;
    }
    if (std::fseek(fp, 0, SEEK_SET) != 0)
    {
        std::fclose(fp);
        return false;
    }

    auto* file_buf = static_cast<uint8_t*>(heap_caps_malloc(static_cast<size_t>(file_sz), kCapsPsram));
    if (file_buf == nullptr)
    {
        std::fclose(fp);
        return false;
    }

    const size_t rd = std::fread(file_buf, 1, static_cast<size_t>(file_sz), fp);
    std::fclose(fp);
    if (rd != static_cast<size_t>(file_sz))
    {
        heap_caps_free(file_buf);
        return false;
    }

    WavInfo info{};
    if (!parse_wav_pcm(file_buf, static_cast<size_t>(file_sz), &info))
    {
        heap_caps_free(file_buf);
        EBOOK_LOGE(TAG, "wav parse fail");
        return false;
    }

    const size_t frame_count = info.data_bytes / (static_cast<size_t>(info.bits / 8U) * info.channels);
    const auto*  src         = static_cast<const int16_t*>(info.data);

    size_t mono_count = 0;
    int16_t* mono     = mono_from_interleaved(src, frame_count, info.channels, &mono_count);
    heap_caps_free(file_buf);
    if (mono == nullptr)
    {
        EBOOK_LOGE(TAG, "mono alloc fail");
        return false;
    }

    size_t dst_count = 0;
    int16_t* dst     = resample_linear(mono, mono_count, info.rate, kDstRate, &dst_count);
    heap_caps_free(mono);
    if (dst == nullptr || dst_count == 0)
    {
        EBOOK_LOGE(TAG, "resample fail");
        return false;
    }

    *out_pcm   = dst;
    *out_count = dst_count;
    *out_rate  = kDstRate;
    EBOOK_LOGI(TAG, "knock ready %u Hz %u samples",
               static_cast<unsigned>(kDstRate),
               static_cast<unsigned>(dst_count));
    return true;
}

} // namespace

WoodenFishSound& WoodenFishSound::get_instance()
{
    static WoodenFishSound s;
    return s;
}

bool WoodenFishSound::init()
{
    if (ready_) return true;
    if (!AudioCodec::get_instance().ready())
    {
        EBOOK_LOGW(TAG, "codec not ready");
        return false;
    }

    int16_t* pcm      = nullptr;
    size_t   count    = 0;
    uint32_t rate     = 0;
    if (!load_knock_pcm(&pcm, &count, &rate))
        return false;

    pcm_       = pcm;
    pcm_count_ = count;
    dst_rate_  = rate;
    ready_     = true;
    return true;
}

void WoodenFishSound::clear_play_task()
{
    if (play_task_ == nullptr) return;
    if (play_task_alive_)
        play_task_->destroy();
    play_task_.reset();
}

void WoodenFishSound::run_play()
{
    play_task_alive_ = true;

    auto finish = [this]() { play_task_alive_ = false; };

    if (!ready_ || pcm_ == nullptr || pcm_count_ == 0)
    {
        finish();
        return;
    }

    if (data::SystemState::get_instance().mute())
    {
        finish();
        return;
    }

    if (!AudioCodec::get_instance().ready())
    {
        finish();
        return;
    }

    if (!AudioCodec::get_instance().ensure_sample_rate(dst_rate_))
    {
        finish();
        return;
    }

    auto& codec = AudioCodec::get_instance().codec();
    AudioCodec::get_instance().apply_system_volume();
    codec.enable_output(true);

    size_t offset = 0;
    const size_t total_bytes = pcm_count_ * sizeof(int16_t);
    while (offset < total_bytes)
    {
        const size_t chunk =
            (total_bytes - offset > kWriteChunk) ? kWriteChunk : (total_bytes - offset);
        const auto* ptr =
            reinterpret_cast<const int16_t*>(reinterpret_cast<const uint8_t*>(pcm_) + offset);
        const int samples = static_cast<int>(chunk / sizeof(int16_t));
        const int wr      = codec.write(ptr, samples);
        if (wr <= 0) break;
        offset += static_cast<size_t>(wr) * sizeof(int16_t);
    }

    ::app::sys::task::TaskMgr::delay_ms(kOutputOffPadMs);
    codec.enable_output(false);
    finish();
}

bool WoodenFishSound::play_knock()
{
    if (!ready_ || pcm_ == nullptr || pcm_count_ == 0) return false;
    if (data::SystemState::get_instance().mute()) return false;
    if (!AudioCodec::get_instance().ready()) return false;
    if (MusicPlayer::get_instance().state() == MusicPlayState::Playing) return false;
    if (play_task_alive_) return false;

    clear_play_task();

    ::app::sys::task::Cfg cfg = ::app::sys::task::Cfg::light("fish_snd", ::app::sys::task::Priority::LOW);
    cfg.stack_size = kPlayStack;
    cfg.use_psram  = false;

    play_task_ = std::make_unique<::app::sys::task::Task>(
        [](void* arg) {
            auto* self = static_cast<WoodenFishSound*>(arg);
            if (self != nullptr) self->run_play();
        },
        cfg, this);

    if (!play_task_->start())
    {
        clear_play_task();
        return false;
    }
    return true;
}

} // namespace app::ebook::platform
