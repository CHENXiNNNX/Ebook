#pragma once

#include <cstdint>

namespace app::ebook::apps::weather {

struct ChinaCity
{
    const char* name;
    const char* adcode; /**< 高德城市编码，6 位数字字符串 */
};

/**
 * @brief 国内常用城市表（NVS: wx.city）
 */
class WeatherCities
{
  public:
    static uint8_t           count();
    static const ChinaCity&  at(uint8_t idx);
    static const ChinaCity*  find_by_name(const char* name);

    /** 按高德 adcode 查城市索引；支持区县级 adcode 归并到市级 */
    static uint8_t find_index_by_adcode(const char* adcode);

    static uint8_t load_index();
    static void    save_index(uint8_t idx);
};

} // namespace app::ebook::apps::weather
