#include "data/clock_provider.hpp"

#include <cstdio>
#include <ctime>

#include "common/time/time.hpp"

namespace app::ebook::data {

namespace {

constexpr const char* kCnWeekdays[7] = {
    "\u5468\u65E5",
    "\u5468\u4E00",
    "\u5468\u4E8C",
    "\u5468\u4E09",
    "\u5468\u56DB",
    "\u5468\u4E94",
    "\u5468\u516D",
};

} // namespace

Clock Clock::now()
{
    const auto ts = static_cast<time_t>(::app::common::time::unix_timestamp_sec());
    struct tm lt = {};
    localtime_r(&ts, &lt);

    Clock c{};
    c.hour        = static_cast<uint8_t>(lt.tm_hour);
    c.minute      = static_cast<uint8_t>(lt.tm_min);
    c.second      = static_cast<uint8_t>(lt.tm_sec);
    c.year        = static_cast<uint16_t>(lt.tm_year + 1900);
    c.month       = static_cast<uint8_t>(lt.tm_mon + 1);
    c.day         = static_cast<uint8_t>(lt.tm_mday);
    c.weekday     = static_cast<uint8_t>(lt.tm_wday);
    c.day_of_year = static_cast<uint16_t>(lt.tm_yday);
    return c;
}

void Clock::format_time_hm(char* buf, size_t buf_size) const
{
    if (buf == nullptr || buf_size == 0) return;
    (void)std::snprintf(buf, buf_size, "%02u:%02u",
                        static_cast<unsigned>(hour),
                        static_cast<unsigned>(minute));
}

void Clock::format_date_cn(char* buf, size_t buf_size) const
{
    if (buf == nullptr || buf_size == 0) return;
    (void)std::snprintf(buf, buf_size, "%u\u5E74%u\u6708%u\u65E5",
                        static_cast<unsigned>(year),
                        static_cast<unsigned>(month),
                        static_cast<unsigned>(day));
}

const char* Clock::weekday_name_cn() const
{
    return (weekday <= 6) ? kCnWeekdays[weekday] : "";
}

} // namespace app::ebook::data
