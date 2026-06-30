#pragma once

#include <cstdint>

namespace app::ebook::apps::clock {

/** 闹钟重复方式 */
enum class AlarmRepeat : uint8_t
{
    Daily    = 0,   ///< 每天
    Weekdays = 1,   ///< 工作日（周一至周五）
    Once     = 2,   ///< 单次，响铃后自动关闭
};

struct AlarmEntry
{
    uint8_t     hour{7};
    uint8_t     minute{0};
    bool        enabled{false};
    AlarmRepeat repeat{AlarmRepeat::Daily};
};

/**
 * @brief 闹钟状态（NVS 持久化；housekeeper 整分 tick 后投递 ClockAlarm）
 */
class ClockStore
{
  public:
    static constexpr uint8_t kMaxAlarms = 6;

    static ClockStore& get_instance();

    void load();
    void save();

    uint8_t           alarm_count() const { return alarm_count_; }
    const AlarmEntry& alarm(uint8_t idx) const;
    AlarmEntry&       alarm_mut(uint8_t idx);

    bool add_alarm();
    void remove_alarm(uint8_t idx);

    const char* repeat_label(AlarmRepeat r) const;

    /** 每秒调用；在整分时刻检查闹钟 */
    void tick(uint8_t hour, uint8_t minute, uint8_t second, uint8_t weekday);

  private:
    ClockStore() = default;

    bool weekday_matches(const AlarmEntry& a, uint8_t weekday) const;
    void try_fire_alarms(uint8_t hour, uint8_t minute, uint8_t weekday);
    void persist_alarm(uint8_t idx) const;
    void load_alarm(uint8_t idx);

    AlarmEntry alarms_[kMaxAlarms]{};
    uint8_t    alarm_count_{0};

    /** 每分钟最多触发一次的去重状态 */
    uint8_t last_fire_hour_{0xFF};
    uint8_t last_fire_minute_{0xFF};
    bool    fired_this_minute_{false};
};

} // namespace app::ebook::apps::clock
