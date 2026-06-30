#include "platform/audio_codec.hpp"

#include "core/log.hpp"
#include "data/system_state.hpp"
#include "es8311_codec.hpp"

static const char* const TAG = "AudioCodec";

namespace app::ebook::platform {

namespace {

using ::app::bsp::driver::audio::es8311::Config;
using ::app::bsp::driver::audio::es8311::Es8311Codec;

Es8311Codec g_codec;

} // namespace

AudioCodec& AudioCodec::get_instance()
{
    static AudioCodec s;
    return s;
}

::app::bsp::driver::audio::es8311::Es8311Codec& AudioCodec::codec()
{
    return g_codec;
}

bool AudioCodec::init(i2c_master_bus_handle_t bus, uint32_t sample_rate_hz)
{
    if (bus == nullptr) return false;
    bus_ = bus;
    return ensure_pcm_format(sample_rate_hz, 1);
}

void AudioCodec::deinit()
{
    if (!ready_) return;
    g_codec.enable_output(false);
    g_codec.deinit();
    ready_       = false;
    bus_         = nullptr;
    sample_rate_ = 16000;
    channels_    = 1;
}

bool AudioCodec::ensure_sample_rate(uint32_t sample_rate_hz)
{
    return ensure_pcm_format(sample_rate_hz, 1);
}

bool AudioCodec::ensure_pcm_format(uint32_t sample_rate_hz, uint8_t channels)
{
    std::lock_guard<std::mutex> lk(mtx_);
    if (bus_ == nullptr)
        return false;
    const uint8_t ch = (channels >= 2) ? 2 : 1;
    if (ready_ && sample_rate_hz == sample_rate_ && ch == channels_)
        return true;

    if (ready_)
    {
        g_codec.enable_output(false);
        g_codec.deinit();
        ready_ = false;
    }

    Config cfg{};
    cfg.sample_rate = sample_rate_hz;
    cfg.channels    = ch;
    if (!g_codec.init(bus_, &cfg))
    {
        EBOOK_LOGW(TAG, "codec init fail %u Hz ch=%u",
                   static_cast<unsigned>(sample_rate_hz),
                   static_cast<unsigned>(ch));
        return false;
    }

    sample_rate_ = sample_rate_hz;
    channels_    = ch;
    ready_       = true;
    apply_system_volume();
    EBOOK_LOGI(TAG, "ready %u Hz ch=%u",
               static_cast<unsigned>(sample_rate_hz),
               static_cast<unsigned>(ch));
    return true;
}

void AudioCodec::apply_system_volume()
{
    if (!ready_) return;
    const auto& st = data::SystemState::get_instance();
    g_codec.set_output_volume(st.mute() ? 0 : st.volume());
    if (g_codec.is_output_enabled())
        g_codec.enable_output(!st.mute());
}

} // namespace app::ebook::platform
