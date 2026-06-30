#include "apps/weather/weather_cities.hpp"

#include <cstring>

#include "data/persist.hpp"

namespace app::ebook::apps::weather {

namespace {

constexpr const char* kKCity = "wx.city";

/** name, adcode（高德行政区划代码） */
constexpr ChinaCity kCities[] = {
    {"\u5317\u4EAC",     "110000"},
    {"\u4E0A\u6D77",     "310000"},
    {"\u5E7F\u5DDE",     "440100"},
    {"\u6DF1\u5733",     "440300"},
    {"\u6210\u90FD",     "510100"},
    {"\u676D\u5DDE",     "330100"},
    {"\u6B66\u6C49",     "420100"},
    {"\u897F\u5B89",     "610100"},
    {"\u5357\u4EAC",     "320100"},
    {"\u5929\u6D25",     "120000"},
    {"\u91CD\u5E86",     "500000"},
    {"\u82CF\u5DDE",     "320500"},
    {"\u90D1\u5DDE",     "410100"},
    {"\u957F\u6C99",     "430100"},
    {"\u6C88\u9633",     "210100"},
    {"\u9752\u5C9B",     "370200"},
    {"\u5927\u8FDE",     "210200"},
    {"\u53A6\u95E8",     "350200"},
    {"\u798F\u5DDE",     "350100"},
    {"\u6D4E\u5357",     "370100"},
    {"\u54C8\u5C14\u6EE8", "230100"},
    {"\u957F\u6625",     "220100"},
    {"\u6606\u660E",     "530100"},
    {"\u8D35\u9633",     "520100"},
    {"\u5357\u5B81",     "450100"},
    {"\u5408\u80A5",     "340100"},
    {"\u77F3\u5BB6\u5E84", "130100"},
    {"\u592A\u539F",     "140100"},
    {"\u5357\u660C",     "360100"},
    {"\u73E0\u6D77",     "440400"},
    {"\u4F5B\u5C71",     "440600"},
};

constexpr uint8_t kCount = static_cast<uint8_t>(sizeof(kCities) / sizeof(kCities[0]));

} // namespace

uint8_t WeatherCities::count() { return kCount; }

const ChinaCity& WeatherCities::at(uint8_t idx)
{
    static const ChinaCity kFallback{"", "110000"};
    return (idx < kCount) ? kCities[idx] : kFallback;
}

const ChinaCity* WeatherCities::find_by_name(const char* name)
{
    if (name == nullptr) return nullptr;
    for (uint8_t i = 0; i < kCount; ++i)
        if (std::strcmp(kCities[i].name, name) == 0)
            return &kCities[i];
    return nullptr;
}

uint8_t WeatherCities::find_index_by_adcode(const char* adcode)
{
    if (adcode == nullptr || std::strlen(adcode) < 6)
    {
        return 0xFF;
    }

    auto try_match = [](const char* code) -> uint8_t {
        for (uint8_t i = 0; i < kCount; ++i)
        {
            if (std::strcmp(kCities[i].adcode, code) == 0)
            {
                return i;
            }
        }
        return 0xFF;
    };

    uint8_t idx = try_match(adcode);
    if (idx != 0xFF)
    {
        return idx;
    }

    char norm[7];
    std::strncpy(norm, adcode, 6);
    norm[6] = '\0';

    // 区县级 → 市级：510107 → 510100
    norm[4] = '0';
    norm[5] = '0';
    idx     = try_match(norm);
    if (idx != 0xFF)
    {
        return idx;
    }

    // 直辖市区县 → 省级：110101 → 110000
    norm[2] = '0';
    norm[3] = '0';
    return try_match(norm);
}

uint8_t WeatherCities::load_index()
{
    uint8_t idx = 0;
    if (data::Persist::get_u8(kKCity, idx) && idx < kCount)
        return idx;
    return 0;
}

void WeatherCities::save_index(uint8_t idx)
{
    if (idx >= kCount) return;
    (void)data::Persist::set_u8(kKCity, idx);
    data::Persist::commit();
}

} // namespace app::ebook::apps::weather
