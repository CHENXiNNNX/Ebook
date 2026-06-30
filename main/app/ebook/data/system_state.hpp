#pragma once

#include <cstdint>

namespace app::ebook::data {

/**
 * @brief 系统运行时状态（UI 任务持有；setter 写 NVS；亮度/音量/夜间模式可注册观察者）
 */
class SystemState
{
  public:
    using ValueObserver = void (*)(uint8_t v);
    using BoolObserver  = void (*)(bool v);

    static SystemState& get_instance();

    void load();
    void save();

    void set_brightness_observer(ValueObserver cb) { bright_cb_ = cb; }
    void set_volume_observer(ValueObserver cb)     { volume_cb_ = cb; }
    void set_night_mode_observer(BoolObserver cb)  { night_cb_ = cb; }

    bool wifi()       const { return wifi_; }
    bool bluetooth()  const { return bluetooth_; }
    bool hotspot()    const { return hotspot_; }
    bool night_mode() const { return night_mode_; }
    bool mute()       const { return mute_; }

    void set_wifi(bool v);
    void set_bluetooth(bool v);
    void set_hotspot(bool v);
    void set_night_mode(bool v);
    void set_mute(bool v);

    uint8_t brightness() const { return brightness_; }
    uint8_t volume()     const { return volume_; }

    void set_brightness(uint8_t v);
    void set_volume(uint8_t v);

    uint8_t  battery_pct() const { return battery_pct_; }
    uint32_t battery_mv()  const { return battery_mv_; }
    void     set_battery(uint8_t pct, uint32_t mv);

  private:
    SystemState() = default;

    ValueObserver bright_cb_{nullptr};
    ValueObserver volume_cb_{nullptr};
    BoolObserver  night_cb_{nullptr};

    bool     wifi_{false};
    bool     bluetooth_{false};
    bool     hotspot_{false};
    bool     night_mode_{false};
    bool     mute_{false};
    uint8_t  brightness_{50};
    uint8_t  volume_{50};
    uint8_t  battery_pct_{0};
    uint32_t battery_mv_{0};
};

} // namespace app::ebook::data
