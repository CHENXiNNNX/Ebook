#pragma once

#include <cstdint>
#include <mutex>

#include <driver/i2c_master.h>

#include "es8311_codec.hpp"

namespace app::ebook::platform {

/** @brief 全机 ES8311；SystemState 观察者触发 apply_system_volume() */
class AudioCodec
{
  public:
    static AudioCodec& get_instance();

    bool init(i2c_master_bus_handle_t bus, uint32_t sample_rate_hz = 16000);
    void deinit();

    bool ensure_pcm_format(uint32_t sample_rate_hz, uint8_t channels = 1);
    bool ensure_sample_rate(uint32_t sample_rate_hz);

    bool ready() const { return ready_; }
    uint32_t sample_rate() const { return sample_rate_; }
    uint8_t  channels()    const { return channels_; }

    ::app::bsp::driver::audio::es8311::Es8311Codec& codec();

    void apply_system_volume();

  private:
    AudioCodec() = default;

    i2c_master_bus_handle_t bus_{nullptr};
    uint32_t                sample_rate_{16000};
    uint8_t                 channels_{1};
    bool                    ready_{false};
    std::mutex              mtx_{};
};

} // namespace app::ebook::platform
