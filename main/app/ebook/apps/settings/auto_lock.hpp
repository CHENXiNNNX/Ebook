#pragma once

#include <cstddef>
#include <cstdint>

namespace app::ebook::apps::settings {

/**
 * @brief 自动锁屏（NVS: disp.autolock.on / disp.autolock.min）
 */
class AutoLock
{
  public:
    static AutoLock& get_instance();

    void load();

    bool     enabled() const { return enabled_; }
    uint8_t  timeout_min() const;
    uint8_t  preset_count() const;
    uint8_t  preset_index() const;

    void set_enabled(bool on);
    void cycle_timeout_preset();

    void notify_activity();
    void tick();

    void format_timeout(char* buf, size_t cap) const;

  private:
    AutoLock() = default;

    static uint32_t now_ms();

    bool     enabled_{false};
    uint8_t  preset_idx_{2};
    uint32_t last_activity_ms_{0};
    bool     lock_hint_sent_{false};
};

} // namespace app::ebook::apps::settings
