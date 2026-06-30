#pragma once

#include <cstddef>
#include <cstdint>

namespace app::ebook::data {

/** @brief 时钟字段（包装 common/time，供 widget 复用格式化） */
struct Clock
{
    uint8_t  hour{0};
    uint8_t  minute{0};
    uint8_t  second{0};
    uint16_t year{0};
    uint8_t  month{0};
    uint8_t  day{0};
    uint8_t  weekday{0};      ///< 0=Sunday
    uint16_t day_of_year{0};

    static Clock now();

    void format_time_hm(char* buf, size_t buf_size) const;
    void format_date_cn(char* buf, size_t buf_size) const;

    const char* weekday_name_cn() const;
};

} // namespace app::ebook::data
