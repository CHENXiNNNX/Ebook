#include "apps/settings/auto_lock.hpp"

#include <cstdio>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "data/persist.hpp"
#include "router/page_id.hpp"
#include "router/router.hpp"
#include "ui/ui_bus.hpp"

namespace app::ebook::apps::settings {

namespace {

constexpr const char* kKOn  = "disp.autolock.on";
constexpr const char* kKMin = "disp.autolock.min";

constexpr uint8_t kPresets[] = {1, 3, 5, 10, 30};
constexpr uint8_t kPresetCount = sizeof(kPresets) / sizeof(kPresets[0]);

uint8_t index_of_min(uint8_t min)
{
    for (uint8_t i = 0; i < kPresetCount; ++i)
    {
        if (kPresets[i] == min)
            return i;
    }
    return 2;
}

} // namespace

AutoLock& AutoLock::get_instance()
{
    static AutoLock s;
    return s;
}

uint32_t AutoLock::now_ms()
{
    return static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

void AutoLock::load()
{
    enabled_     = false;
    preset_idx_  = 2;
    last_activity_ms_ = now_ms();
    lock_hint_sent_   = false;

    bool on = false;
    if (data::Persist::get_bool(kKOn, on))
        enabled_ = on;

    uint8_t min = kPresets[preset_idx_];
    if (data::Persist::get_u8(kKMin, min))
        preset_idx_ = index_of_min(min);
}

uint8_t AutoLock::timeout_min() const
{
    return kPresets[preset_idx_ < kPresetCount ? preset_idx_ : 0];
}

uint8_t AutoLock::preset_count() const
{
    return kPresetCount;
}

uint8_t AutoLock::preset_index() const
{
    return preset_idx_;
}

void AutoLock::set_enabled(bool on)
{
    if (on == enabled_)
        return;
    enabled_ = on;
    (void)data::Persist::set_bool(kKOn, on);
    data::Persist::commit();
    notify_activity();
}

void AutoLock::cycle_timeout_preset()
{
    preset_idx_ = static_cast<uint8_t>((preset_idx_ + 1U) % kPresetCount);
    (void)data::Persist::set_u8(kKMin, timeout_min());
    data::Persist::commit();
    notify_activity();
}

void AutoLock::notify_activity()
{
    last_activity_ms_ = now_ms();
    lock_hint_sent_   = false;
}

void AutoLock::format_timeout(char* buf, size_t cap) const
{
    if (buf == nullptr || cap == 0U)
        return;
    const unsigned min = static_cast<unsigned>(timeout_min());
    (void)std::snprintf(buf, cap, "%u\u5206\u949F", min);
}

void AutoLock::tick()
{
    if (!enabled_)
        return;

    if (router::Router::instance().stack().shell_top() == router::ShellPage::Lock)
    {
        lock_hint_sent_ = false;
        return;
    }

    const uint32_t limit_ms =
        static_cast<uint32_t>(timeout_min()) * 60U * 1000U;
    const uint32_t idle_ms = now_ms() - last_activity_ms_;
    if (idle_ms < limit_ms || lock_hint_sent_)
        return;

    lock_hint_sent_ = true;
    (void)ui::UiBus::get_instance().post_system_hint(ui::SystemHintKind::AutoLock);
}

} // namespace app::ebook::apps::settings
