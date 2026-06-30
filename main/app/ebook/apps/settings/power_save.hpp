#pragma once

#include <cstdint>

namespace app::ebook::apps::settings {

/** @brief L1 省电模式（背光上限 + 关无线）；低电量阈值提示 */
class PowerSave
{
  public:
    enum class Level : uint8_t
    {
        Normal = 0,
        Strong,
    };

    static PowerSave& get_instance();

    void load();

    bool  enabled() const { return enabled_; }
    bool  auto_enabled() const { return auto_enabled_; }
    Level level() const { return level_; }

    void toggle_manual();
    void set_enabled(bool on, Level level, bool from_auto);
    void on_battery_pct(uint8_t pct);

  private:
    PowerSave() = default;

    void apply_policy();
    void restore_snapshot();
    void capture_snapshot();

    void prompt_enable(Level level);
    void prompt_disable();

    static void on_enable_choice(bool accepted, void* user);
    static void on_disable_choice(bool accepted, void* user);

    static constexpr uint8_t kThreshLow20  = 20;
    static constexpr uint8_t kThreshLow10  = 10;
    static constexpr uint8_t kThreshHigh80 = 80;
    static constexpr uint8_t kCapNormal    = 30;
    static constexpr uint8_t kCapStrong    = 15;

    bool     enabled_{false};
    bool     auto_enabled_{false};
    Level    level_{Level::Normal};
    uint8_t  last_pct_{0xFF};
    bool     snapshot_valid_{false};
    uint8_t  snap_brightness_{50};
    bool     snap_wifi_{false};
    bool     snap_bt_{false};
    bool     snap_hotspot_{false};
    Level    pending_level_{Level::Normal};
};

} // namespace app::ebook::apps::settings
