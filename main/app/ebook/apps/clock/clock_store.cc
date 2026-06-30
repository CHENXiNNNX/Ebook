#include "apps/clock/clock_store.hpp"

#include <cstdio>

#include "data/persist.hpp"
#include "ui/ui_bus.hpp"
#include "ui/ui_event.hpp"

namespace app::ebook::apps::clock {

namespace {

AlarmRepeat repeat_from_u8(uint8_t v)
{
    return (v <= 2) ? static_cast<AlarmRepeat>(v) : AlarmRepeat::Daily;
}

void make_key(char* buf, size_t cap, uint8_t idx, char suffix)
{
    (void)std::snprintf(buf, cap, "clk.a%u%c",
                        static_cast<unsigned>(idx), suffix);
}

} // namespace

ClockStore& ClockStore::get_instance()
{
    static ClockStore s;
    return s;
}

void ClockStore::load()
{
    alarm_count_ = 0;
    uint8_t n = 0;
    if (data::Persist::get_u8("clk.n", n) && n <= kMaxAlarms)
        alarm_count_ = n;

    for (uint8_t i = 0; i < alarm_count_; ++i)
        load_alarm(i);
}

void ClockStore::save()
{
    (void)data::Persist::set_u8("clk.n", alarm_count_);
    for (uint8_t i = 0; i < alarm_count_; ++i)
        persist_alarm(i);
    data::Persist::commit();
}

void ClockStore::load_alarm(uint8_t idx)
{
    if (idx >= kMaxAlarms) return;

    char key[12];
    AlarmEntry& a = alarms_[idx];

    make_key(key, sizeof(key), idx, 'h');
    uint8_t v = 0;
    if (data::Persist::get_u8(key, v)) a.hour = v;

    make_key(key, sizeof(key), idx, 'm');
    if (data::Persist::get_u8(key, v)) a.minute = v;

    make_key(key, sizeof(key), idx, 'e');
    bool en = false;
    if (data::Persist::get_bool(key, en)) a.enabled = en;

    make_key(key, sizeof(key), idx, 'r');
    if (data::Persist::get_u8(key, v)) a.repeat = repeat_from_u8(v);
}

void ClockStore::persist_alarm(uint8_t idx) const
{
    if (idx >= kMaxAlarms) return;

    char key[12];
    const AlarmEntry& a = alarms_[idx];

    make_key(key, sizeof(key), idx, 'h'); (void)data::Persist::set_u8 (key, a.hour);
    make_key(key, sizeof(key), idx, 'm'); (void)data::Persist::set_u8 (key, a.minute);
    make_key(key, sizeof(key), idx, 'e'); (void)data::Persist::set_bool(key, a.enabled);
    make_key(key, sizeof(key), idx, 'r'); (void)data::Persist::set_u8 (key, static_cast<uint8_t>(a.repeat));
}

const AlarmEntry& ClockStore::alarm(uint8_t idx) const
{
    static const AlarmEntry kEmpty{};
    return (idx < alarm_count_) ? alarms_[idx] : kEmpty;
}

AlarmEntry& ClockStore::alarm_mut(uint8_t idx)
{
    static AlarmEntry kEmpty{};
    return (idx < alarm_count_) ? alarms_[idx] : kEmpty;
}

bool ClockStore::add_alarm()
{
    if (alarm_count_ >= kMaxAlarms) return false;
    AlarmEntry& a = alarms_[alarm_count_];
    a         = {};
    a.hour    = 7;
    a.minute  = 0;
    a.enabled = true;
    a.repeat  = AlarmRepeat::Daily;
    ++alarm_count_;
    return true;
}

void ClockStore::remove_alarm(uint8_t idx)
{
    if (idx >= alarm_count_) return;
    for (uint8_t i = idx; i + 1 < alarm_count_; ++i)
        alarms_[i] = alarms_[i + 1];
    --alarm_count_;
    save();
}

const char* ClockStore::repeat_label(AlarmRepeat r) const
{
    switch (r)
    {
        case AlarmRepeat::Daily:    return "\u6BCF\u5929";
        case AlarmRepeat::Weekdays: return "\u5DE5\u4F5C\u65E5";
        case AlarmRepeat::Once:     return "\u5355\u6B21";
    }
    return "";
}

bool ClockStore::weekday_matches(const AlarmEntry& a, uint8_t weekday) const
{
    switch (a.repeat)
    {
        case AlarmRepeat::Daily:    return true;
        case AlarmRepeat::Weekdays: return weekday >= 1 && weekday <= 5;
        case AlarmRepeat::Once:     return true;
    }
    return false;
}

void ClockStore::try_fire_alarms(uint8_t hour, uint8_t minute, uint8_t weekday)
{
    if (last_fire_hour_ != hour || last_fire_minute_ != minute)
    {
        last_fire_hour_     = hour;
        last_fire_minute_   = minute;
        fired_this_minute_  = false;
    }
    if (fired_this_minute_) return;

    for (uint8_t i = 0; i < alarm_count_; ++i)
    {
        const AlarmEntry& a = alarms_[i];
        if (!a.enabled) continue;
        if (a.hour != hour || a.minute != minute) continue;
        if (!weekday_matches(a, weekday)) continue;

        fired_this_minute_ = true;
        if (a.repeat == AlarmRepeat::Once)
        {
            alarms_[i].enabled = false;
            save();
        }
        (void)ui::UiBus::get_instance().post_system_hint(ui::SystemHintKind::ClockAlarm, i);
        break;
    }
}

void ClockStore::tick(uint8_t hour, uint8_t minute, uint8_t second, uint8_t weekday)
{
    if (second == 0)
        try_fire_alarms(hour, minute, weekday);
}

} // namespace app::ebook::apps::clock
