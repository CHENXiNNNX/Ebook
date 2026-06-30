#include "platform/music_player.hpp"

#include <cstdio>
#include <cstring>
#include <strings.h>

#include <esp_heap_caps.h>

#include "common/time/time.hpp"
#include "core/log.hpp"
#include "data/system_state.hpp"
#include "platform/audio_codec.hpp"
#include "system/task/task.hpp"
#include "ui/ui_bus.hpp"
#include "ui/ui_event.hpp"

#include "esp_audio_dec_default.h"
#include "esp_audio_simple_dec.h"
#include "esp_audio_simple_dec_default.h"

static const char* const TAG = "MusicPlayer";

namespace app::ebook::platform {

namespace {

constexpr size_t   kPlayStack  = 20480;
constexpr size_t   kReadChunk  = 4096;
constexpr size_t   kOutBufInit = 8192;
constexpr uint32_t kStopWaitMs = 3000;
constexpr uint32_t kProgressMs = 1000;

constexpr uint32_t kCapsPsram = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;

void* alloc_psram(size_t size)
{
    return heap_caps_malloc(size, kCapsPsram);
}

void* realloc_psram(void* ptr, size_t size)
{
    return heap_caps_realloc(ptr, size, kCapsPsram);
}

esp_audio_simple_dec_type_t dec_type_from_path(const char* path)
{
    if (path == nullptr) return ESP_AUDIO_SIMPLE_DEC_TYPE_NONE;
    const char* ext = std::strrchr(path, '.');
    if (ext == nullptr) return ESP_AUDIO_SIMPLE_DEC_TYPE_NONE;
    ++ext;
    if (strcasecmp(ext, "mp3") == 0) return ESP_AUDIO_SIMPLE_DEC_TYPE_MP3;
    if (strcasecmp(ext, "wav") == 0) return ESP_AUDIO_SIMPLE_DEC_TYPE_WAV;
    return ESP_AUDIO_SIMPLE_DEC_TYPE_NONE;
}

size_t write_pcm(const uint8_t* data, size_t length, bool stop)
{
    if (stop || data == nullptr || length == 0) return 0;
    if (data::SystemState::get_instance().mute()) return length;
    if (!AudioCodec::get_instance().ready()) return 0;

    auto& codec  = AudioCodec::get_instance().codec();
    const int n  = static_cast<int>(length / sizeof(int16_t));
    const int wr = codec.write(reinterpret_cast<const int16_t*>(data), n);
    if (wr <= 0) return 0;
    return static_cast<size_t>(wr) * sizeof(int16_t);
}

bool wait_state_clear(volatile MusicPlayState& state, MusicPlayState match)
{
    const int64_t t0 = ::app::common::time::uptime_ms();
    while (state == match)
    {
        if ((::app::common::time::uptime_ms() - t0) > static_cast<int64_t>(kStopWaitMs))
            return false;
        ::app::sys::task::TaskMgr::delay_ms(20);
    }
    return true;
}

} // namespace

MusicPlayer& MusicPlayer::get_instance()
{
    static MusicPlayer s;
    return s;
}

bool MusicPlayer::ensure_decoders()
{
    if (dec_registered_) return true;
    if (esp_audio_dec_register_default() != ESP_AUDIO_ERR_OK ||
        esp_audio_simple_dec_register_default() != ESP_AUDIO_ERR_OK)
    {
        EBOOK_LOGE(TAG, "decoder register fail");
        return false;
    }
    dec_registered_ = true;
    return true;
}

bool MusicPlayer::init()
{
    if (!AudioCodec::get_instance().ready())
    {
        EBOOK_LOGW(TAG, "audio codec not ready");
        return false;
    }
    return ensure_decoders();
}

void MusicPlayer::clear_play_task()
{
    if (play_task_ == nullptr) return;
    // 任务已 vTaskDelete 时不可再 destroy
    if (play_task_alive_)
        play_task_->destroy();
    play_task_.reset();
}

void MusicPlayer::set_state(MusicPlayState s)
{
    state_ = s;
    post_state_hint();
}

void MusicPlayer::post_state_hint()
{
    (void)ui::UiBus::get_instance().post_system_hint(
        ui::SystemHintKind::MusicStateChanged, static_cast<uint32_t>(state_));
}

void MusicPlayer::post_progress_hint()
{
    (void)ui::UiBus::get_instance().post_system_hint(
        ui::SystemHintKind::MusicProgress, static_cast<uint32_t>(elapsed_sec()));
}

uint16_t MusicPlayer::elapsed_sec() const
{
    if (play_start_us_ <= 0) return 0;
    const int64_t us = ::app::common::time::uptime_us() - play_start_us_;
    if (us < 0) return 0;
    return static_cast<uint16_t>(us / 1000000LL);
}

void MusicPlayer::stop()
{
    pause_end_ = false;
    stop_req_  = true;

    volatile MusicPlayState* s = &state_;
    const int64_t t0 = ::app::common::time::uptime_ms();
    while (*s == MusicPlayState::Loading || *s == MusicPlayState::Playing)
    {
        if ((::app::common::time::uptime_ms() - t0) > static_cast<int64_t>(kStopWaitMs))
            break;
        ::app::sys::task::TaskMgr::delay_ms(20);
    }

    stop_req_      = false;
    state_         = MusicPlayState::Idle;
    play_start_us_ = 0;
    clear_play_task();
    AudioCodec::get_instance().codec().enable_output(false);
}

bool MusicPlayer::play(const char* path)
{
    if (path == nullptr || path[0] == '\0') return false;
    stop();

    std::strncpy(path_, path, sizeof(path_) - 1);
    path_[sizeof(path_) - 1] = '\0';

    stop_req_ = false;
    set_state(MusicPlayState::Loading);

    if (!AudioCodec::get_instance().ready())
    {
        last_error_ = 5;
        set_state(MusicPlayState::Error);
        return false;
    }
    if (!ensure_decoders())
    {
        last_error_ = 6;
        set_state(MusicPlayState::Error);
        return false;
    }

    ::app::sys::task::Cfg cfg;
    cfg.name       = "music_play";
    cfg.stack_size = kPlayStack;
    cfg.priority   = ::app::sys::task::Priority::NORMAL;
    cfg.use_psram  = true;

    play_task_ = std::make_unique<::app::sys::task::Task>(
        [](void* arg) {
            auto* self = static_cast<MusicPlayer*>(arg);
            if (self != nullptr) self->run_playback(self->path_);
        },
        cfg, this);

    if (!play_task_->start())
    {
        EBOOK_LOGE(TAG, "music_play task start fail");
        play_task_alive_ = false;
        play_task_.reset();
        last_error_ = 3;
        set_state(MusicPlayState::Error);
        return false;
    }
    play_task_alive_ = true;
    return true;
}

void MusicPlayer::pause()
{
    if (state_ != MusicPlayState::Playing) return;
    pause_end_ = true;
    stop_req_  = true;

    (void)wait_state_clear(state_, MusicPlayState::Playing);

    stop_req_  = false;
    pause_end_ = false;
    clear_play_task();
    AudioCodec::get_instance().codec().enable_output(false);
    set_state(MusicPlayState::Paused);
}

void MusicPlayer::resume()
{
    if (state_ != MusicPlayState::Paused || path_[0] == '\0') return;
    (void)play(path_);
}

bool MusicPlayer::try_start_output(esp_audio_simple_dec_handle_t dec)
{
    esp_audio_simple_dec_info_t info = {};
    if (esp_audio_simple_dec_get_info(dec, &info) != ESP_AUDIO_ERR_OK || info.sample_rate == 0)
        return false;

    EBOOK_LOGI(TAG, "PCM %u Hz ch=%u",
               static_cast<unsigned>(info.sample_rate),
               static_cast<unsigned>(info.channel));

    (void)AudioCodec::get_instance().ensure_pcm_format(info.sample_rate, info.channel);
    AudioCodec::get_instance().codec().enable_output(true);
    AudioCodec::get_instance().apply_system_volume();

    set_state(MusicPlayState::Playing);
    play_start_us_ = ::app::common::time::uptime_us();
    return true;
}

bool MusicPlayer::decode_raw(esp_audio_simple_dec_handle_t dec,
                             esp_audio_simple_dec_raw_t* raw,
                             uint8_t** out_buf, size_t* out_cap,
                             bool* started, bool* failed)
{
    while (raw->len > 0 && !stop_req_)
    {
        esp_audio_simple_dec_out_t out_frame{};
        out_frame.buffer = *out_buf;
        out_frame.len    = static_cast<uint32_t>(*out_cap);

        const esp_audio_err_t ret = esp_audio_simple_dec_process(dec, raw, &out_frame);
        if (ret == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH)
        {
            auto* nb = static_cast<uint8_t*>(realloc_psram(*out_buf, out_frame.needed_size));
            if (nb == nullptr)
            {
                EBOOK_LOGE(TAG, "out buf realloc %u fail",
                           static_cast<unsigned>(out_frame.needed_size));
                *failed = true;
                return false;
            }
            *out_buf = nb;
            *out_cap = out_frame.needed_size;
            continue;
        }
        if (ret != ESP_AUDIO_ERR_OK)
        {
            EBOOK_LOGE(TAG, "decode failed %d", static_cast<int>(ret));
            *failed = true;
            return false;
        }

        if (out_frame.decoded_size > 0)
        {
            if (!*started && try_start_output(dec)) *started = true;
            (void)write_pcm(out_frame.buffer, out_frame.decoded_size, stop_req_);
        }

        raw->len    -= raw->consumed;
        raw->buffer += raw->consumed;
    }
    return true;
}

void MusicPlayer::run_playback(const char* path)
{
    play_task_alive_ = true;
    last_error_    = 0;
    duration_sec_  = 0;
    play_start_us_ = 0;

    if (!AudioCodec::get_instance().ready())
    {
        last_error_ = 5;
        set_state(MusicPlayState::Error);
        play_task_alive_ = false;
        return;
    }

    const esp_audio_simple_dec_type_t ftype = dec_type_from_path(path);
    if (ftype == ESP_AUDIO_SIMPLE_DEC_TYPE_NONE)
    {
        EBOOK_LOGE(TAG, "unsupported format: %s", path);
        last_error_ = 4;
        set_state(MusicPlayState::Error);
        play_task_alive_ = false;
        return;
    }

    if (!ensure_decoders())
    {
        last_error_ = 6;
        set_state(MusicPlayState::Error);
        play_task_alive_ = false;
        return;
    }

    FILE* fp = std::fopen(path, "rb");
    if (fp == nullptr)
    {
        EBOOK_LOGE(TAG, "fopen fail: %s", path);
        last_error_ = 2;
        set_state(MusicPlayState::Error);
        play_task_alive_ = false;
        return;
    }

    esp_audio_simple_dec_handle_t dec = nullptr;
    esp_audio_simple_dec_cfg_t dec_cfg = {
        .dec_type      = ftype,
        .dec_cfg       = nullptr,
        .cfg_size      = 0,
        .use_frame_dec = false,
    };
    if (esp_audio_simple_dec_open(&dec_cfg, &dec) != ESP_AUDIO_ERR_OK)
    {
        std::fclose(fp);
        EBOOK_LOGE(TAG, "dec open fail type=%d path=%s", static_cast<int>(ftype), path);
        last_error_ = 4;
        set_state(MusicPlayState::Error);
        play_task_alive_ = false;
        return;
    }

    auto* in_buf  = static_cast<uint8_t*>(alloc_psram(kReadChunk));
    auto* out_buf = static_cast<uint8_t*>(alloc_psram(kOutBufInit));
    size_t out_cap = kOutBufInit;

    auto cleanup = [&]() {
        esp_audio_simple_dec_close(dec);
        std::fclose(fp);
        heap_caps_free(in_buf);
        heap_caps_free(out_buf);
        AudioCodec::get_instance().codec().enable_output(false);
    };

    if (in_buf == nullptr || out_buf == nullptr)
    {
        EBOOK_LOGE(TAG, "decode buf alloc fail in=%p out=%p",
                   static_cast<void*>(in_buf), static_cast<void*>(out_buf));
        cleanup();
        last_error_ = 3;
        set_state(MusicPlayState::Error);
        play_task_alive_ = false;
        return;
    }

    bool failed = false, started = false;
    uint32_t last_prog_ms = 0;

    while (!stop_req_ && !failed)
    {
        const size_t nread = std::fread(in_buf, 1, kReadChunk, fp);
        const bool   eof   = (nread < kReadChunk) || (std::feof(fp) != 0);

        if (nread > 0)
        {
            esp_audio_simple_dec_raw_t raw{};
            raw.buffer        = in_buf;
            raw.len           = static_cast<uint32_t>(nread);
            raw.eos           = eof;
            raw.consumed      = 0;
            raw.frame_recover = ESP_AUDIO_SIMPLE_DEC_RECOVERY_NONE;
            if (!decode_raw(dec, &raw, &out_buf, &out_cap, &started, &failed)) break;
        }

        if (eof)
        {
            esp_audio_simple_dec_raw_t flush{};
            flush.eos           = true;
            flush.frame_recover = ESP_AUDIO_SIMPLE_DEC_RECOVERY_NONE;
            (void)decode_raw(dec, &flush, &out_buf, &out_cap, &started, &failed);
            break;
        }

        if (nread == 0) { failed = true; break; }

        if (started)
        {
            const uint32_t now_ms = static_cast<uint32_t>(::app::common::time::uptime_ms());
            if (now_ms - last_prog_ms >= kProgressMs)
            {
                last_prog_ms = now_ms;
                post_progress_hint();
            }
        }

        ::app::sys::task::TaskMgr::delay_ms(1);
    }

    cleanup();

    const bool user_stop = stop_req_;
    const bool paused    = pause_end_;
    stop_req_            = false;
    pause_end_           = false;
    play_start_us_       = 0;

    if (failed || (!started && !user_stop && !paused))
    {
        last_error_ = 4;
        set_state(MusicPlayState::Error);
    }
    else if (paused)
        set_state(MusicPlayState::Paused);
    else
        set_state(MusicPlayState::Idle);

    play_task_alive_ = false;
}

} // namespace app::ebook::platform
