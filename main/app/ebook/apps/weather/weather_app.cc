#include "apps/weather/weather_app.hpp"

#include <cstdio>
#include <cstring>
#include <string>

#include <cJSON.h>
#include "sdkconfig.h"

#include "apps/weather/weather_cities.hpp"
#include "gfx/icon.hpp"
#include "network/wifi/wifi.hpp"
#include "protocol/http/http.hpp"
#include "router/refresh_intent.hpp"
#include "ui/strings.hpp"
#include "ui/theme.hpp"
#include "ui/ui_bus.hpp"
#include "ui/widgets.hpp"

namespace app::ebook::apps::weather {

namespace {

using ui::Theme;

constexpr uint32_t kFetchStack    = 8192;
constexpr int32_t  kHttpTimeoutMs = 12000;

const char* amap_web_key()
{
#if defined(CONFIG_EBOOK_AMAP_WEATHER_KEY)
    return CONFIG_EBOOK_AMAP_WEATHER_KEY;
#else
    return "";
#endif
}

bool amap_key_ready()
{
    const char* k = amap_web_key();
    return k != nullptr && k[0] != '\0';
}

core::Rect city_switch_rect()
{
    return core::Rect{
        Theme::kPadLg, static_cast<int16_t>(Theme::kListStartY + 4),
        static_cast<uint16_t>(Theme::kScreenW - Theme::kPadLg * 2), 28};
}

bool amap_status_ok(cJSON* root)
{
    const cJSON* st = cJSON_GetObjectItem(root, "status");
    return cJSON_IsString(st) && st->valuestring != nullptr &&
           std::strcmp(st->valuestring, "1") == 0;
}

int16_t parse_temp_str(const char* s)
{
    if (s == nullptr || s[0] == '\0')
    {
        return 0;
    }
    return static_cast<int16_t>(std::atoi(s));
}

void copy_desc(char* dst, size_t cap, const char* src)
{
    if (dst == nullptr || cap == 0)
    {
        return;
    }
    if (src == nullptr)
    {
        dst[0] = '\0';
        return;
    }
    std::strncpy(dst, src, cap - 1);
    dst[cap - 1] = '\0';
}

bool http_get_text(const char* url, std::string& out)
{
    ::app::protocol::http::HttpResponse resp;
    if (!::app::protocol::http::HttpMgr::get_instance().get(url, resp, kHttpTimeoutMs))
    {
        return false;
    }
    out.assign(resp.body.begin(), resp.body.end());
    return true;
}

bool parse_amap_live(const char* json, int16_t& cur, char* desc, size_t desc_len)
{
    if (json == nullptr)
    {
        return false;
    }
    cJSON* root = cJSON_Parse(json);
    if (root == nullptr)
    {
        return false;
    }

    bool ok = false;
    if (amap_status_ok(root))
    {
        const cJSON* lives = cJSON_GetObjectItem(root, "lives");
        if (cJSON_IsArray(lives) && cJSON_GetArraySize(lives) > 0)
        {
            const cJSON* live = cJSON_GetArrayItem(lives, 0);
            const cJSON* temp = cJSON_GetObjectItem(live, "temperature");
            const cJSON* wx   = cJSON_GetObjectItem(live, "weather");
            if (cJSON_IsString(temp))
            {
                cur = parse_temp_str(temp->valuestring);
                ok  = true;
            }
            if (cJSON_IsString(wx))
            {
                copy_desc(desc, desc_len, wx->valuestring);
            }
        }
    }

    cJSON_Delete(root);
    return ok;
}

bool parse_amap_forecast(const char* json, int16_t& tmax, int16_t& tmin, char* desc, size_t desc_len)
{
    if (json == nullptr)
    {
        return false;
    }
    cJSON* root = cJSON_Parse(json);
    if (root == nullptr)
    {
        return false;
    }

    bool ok = false;
    if (amap_status_ok(root))
    {
        const cJSON* forecasts = cJSON_GetObjectItem(root, "forecasts");
        if (cJSON_IsArray(forecasts) && cJSON_GetArraySize(forecasts) > 0)
        {
            const cJSON* fc = cJSON_GetArrayItem(forecasts, 0);
            const cJSON* casts = cJSON_GetObjectItem(fc, "casts");
            if (cJSON_IsArray(casts) && cJSON_GetArraySize(casts) > 0)
            {
                const cJSON* today = cJSON_GetArrayItem(casts, 0);
                const cJSON* dayt  = cJSON_GetObjectItem(today, "daytemp");
                const cJSON* night = cJSON_GetObjectItem(today, "nighttemp");
                const cJSON* wx    = cJSON_GetObjectItem(today, "dayweather");
                if (cJSON_IsString(dayt) && cJSON_IsString(night))
                {
                    tmax = parse_temp_str(dayt->valuestring);
                    tmin = parse_temp_str(night->valuestring);
                    ok   = true;
                }
                if (desc[0] == '\0' && cJSON_IsString(wx))
                {
                    copy_desc(desc, desc_len, wx->valuestring);
                }
            }
        }
    }

    cJSON_Delete(root);
    return ok;
}

// 返回值：0 成功，2 网络失败，3 解析失败，4 无 Key
uint8_t fetch_amap_weather(const char* adcode,
                           int16_t&     cur,
                           int16_t&     tmax,
                           int16_t&     tmin,
                           char*        desc,
                           size_t       desc_len)
{
    if (!amap_key_ready())
    {
        return 4;
    }

    const char* key = amap_web_key();
    char        url[360];
    std::string body;

    cur     = 0;
    tmax    = 0;
    tmin    = 0;
    desc[0] = '\0';

    (void)std::snprintf(url, sizeof(url),
                        "http://restapi.amap.com/v3/weather/weatherInfo?"
                        "key=%s&city=%s&extensions=base",
                        key, adcode);
    if (!http_get_text(url, body))
    {
        return 2;
    }
    if (!parse_amap_live(body.c_str(), cur, desc, desc_len))
    {
        return 3;
    }

    body.clear();
    (void)std::snprintf(url, sizeof(url),
                        "http://restapi.amap.com/v3/weather/weatherInfo?"
                        "key=%s&city=%s&extensions=all",
                        key, adcode);
    if (http_get_text(url, body))
    {
        (void)parse_amap_forecast(body.c_str(), tmax, tmin, desc, desc_len);
    }

    return 0;
}

} // namespace

void WeatherApp::fetch_worker(void* arg)
{
    auto* self = static_cast<WeatherApp*>(arg);
    uint8_t result = 2;

    if (!self->fetch_cancel_)
    {
        if (!::app::network::wifi::WiFiMgr::get_instance().is_connected())
        {
            result = 1;
        }
        else if (!amap_key_ready())
        {
            result = 4;
        }
        else
        {
            if (self->ip_locate_next_)
            {
                self->ip_locate_next_ = false;
                self->apply_ip_locate();
            }

            const ChinaCity& city = WeatherCities::at(self->city_idx_);
            int16_t          cur  = 0;
            int16_t          tmax = 0;
            int16_t          tmin = 0;
            char             desc[16]{};

            if (!self->fetch_cancel_)
            {
                const uint8_t rc =
                    fetch_amap_weather(city.adcode, cur, tmax, tmin, desc, sizeof(desc));
                if (rc == 0)
                {
                    self->temp_c_     = cur;
                    self->temp_max_   = tmax;
                    self->temp_min_   = tmin;
                    self->data_valid_ = true;
                    copy_desc(self->weather_desc_, sizeof(self->weather_desc_), desc);
                    result = 0;
                }
                else
                {
                    result = rc;
                }
            }
        }
    }

    self->fetching_ = false;
    if (!self->fetch_cancel_)
    {
        (void)ui::UiBus::get_instance().post_system_hint(
            ui::SystemHintKind::WeatherFetchDone, result);
    }
}

WeatherApp& WeatherApp::instance()
{
    static WeatherApp s;
    return s;
}

const char* WeatherApp::title()   const { return ui::strings::kAppWeather; }
uint32_t    WeatherApp::icon_cp() const { return gfx::icon::kFaCloudSun; }

void WeatherApp::apply_ip_locate()
{
    if (!amap_key_ready())
    {
        return;
    }

    char url[200];
    (void)std::snprintf(url, sizeof(url),
                        "http://restapi.amap.com/v3/ip?key=%s", amap_web_key());

    std::string body;
    if (!http_get_text(url, body))
    {
        return;
    }

    cJSON* root = cJSON_Parse(body.c_str());
    if (root == nullptr)
    {
        return;
    }

    if (amap_status_ok(root))
    {
        const cJSON* ad = cJSON_GetObjectItem(root, "adcode");
        if (cJSON_IsString(ad) && ad->valuestring != nullptr && ad->valuestring[0] != '\0' &&
            std::strcmp(ad->valuestring, "[]") != 0)
        {
            const uint8_t idx = WeatherCities::find_index_by_adcode(ad->valuestring);
            if (idx != 0xFF)
            {
                city_idx_ = idx;
                WeatherCities::save_index(idx);
            }
        }
    }

    cJSON_Delete(root);
}

void WeatherApp::on_enter()
{
    view_            = View::Main;
    city_idx_        = WeatherCities::load_index();
    fetch_cancel_    = false;
    ip_locate_next_  = true;
    city_list_.set_area(Theme::list_region());
    city_list_.set_provider([](uint8_t i, ui::widgets::RowStyle& s) {
        s.label        = WeatherCities::at(i).name;
        s.show_chevron = false;
    });
    start_fetch();
}

void WeatherApp::on_exit()
{
    fetch_cancel_ = true;
    fetching_     = false;
    fetch_task_.reset();
}

void WeatherApp::start_fetch()
{
    if (fetching_) return;
    fetching_     = true;
    data_valid_   = false;
    last_error_   = 0;
    fetch_cancel_ = false;
    request_repaint();

    fetch_task_.reset();

    ::app::sys::task::Cfg cfg = ::app::sys::task::Cfg::light("wx_fetch",
                                                             ::app::sys::task::Priority::LOW);
    cfg.stack_size = kFetchStack;
    cfg.use_psram  = true;

    fetch_task_ = std::make_unique<::app::sys::task::Task>(
        &WeatherApp::fetch_worker, cfg, this);

    if (!fetch_task_->start())
    {
        fetch_task_.reset();
        fetching_ = false;
        request_repaint();
    }
}

void WeatherApp::on_ui_event(const ui::UiEvent& ev)
{
    if (ev.kind != ui::UiEventKind::SystemHint) return;
    if (ev.payload.system.hint != ui::SystemHintKind::WeatherFetchDone) return;
    if (fetch_cancel_) return;

    fetching_   = false;
    last_error_ = static_cast<uint8_t>(ev.payload.system.value);
    if (last_error_ != 0) data_valid_ = false;
    fetch_task_.reset();
    request_repaint();
}

void WeatherApp::paint_main(gfx::Canvas& c)
{
    const ChinaCity& city = WeatherCities::at(city_idx_);

    gfx::Canvas::TextStyle ts{};
    ts.size_px = Theme::kFontBody;
    ts.h       = gfx::HAlign::Center;
    ts.v       = gfx::VAlign::Middle;

    const core::Rect switch_row = city_switch_rect();
    c.rect(switch_row, 1);
    char city_line[40];
    (void)std::snprintf(city_line, sizeof(city_line), "%s  %s",
                        city.name, ui::strings::kWxSwitchCity);
    c.text_in(switch_row, city_line, ts);

    const core::Rect body{
        0, static_cast<int16_t>(switch_row.bottom() + 8), Theme::kScreenW,
        static_cast<uint16_t>(Theme::kScreenH - switch_row.bottom() - 8)};

    if (fetching_)
    {
        c.text_in(body, ui::strings::kWxLoading, ts);
        return;
    }
    if (last_error_ == 1)
    {
        c.text_in(body, ui::strings::kNeedWifiFirst, ts);
        return;
    }
    if (last_error_ == 4)
    {
        c.text_in(body, ui::strings::kWxNoKey, ts);
        return;
    }
    if (last_error_ == 2)
    {
        c.text_in(body, ui::strings::kWxNetFail, ts);
        return;
    }
    if (last_error_ == 3 || !data_valid_)
    {
        c.text_in(body, ui::strings::kWxParseFail, ts);
        return;
    }

    char temp_buf[16];
    (void)std::snprintf(temp_buf, sizeof(temp_buf), "%d\u00B0C", static_cast<int>(temp_c_));
    const core::Rect temp_box{body.x, static_cast<int16_t>(body.y + 8), body.w, 48};
    ts.size_px = Theme::kFontHuge;
    c.text_in(temp_box, temp_buf, ts);

    const core::Rect desc_box{
        body.x, static_cast<int16_t>(temp_box.bottom() + 4), body.w, 20};
    ts.size_px = Theme::kFontTitle;
    c.text_in(desc_box, weather_desc_, ts);

    char range_buf[32];
    if (temp_max_ != 0 || temp_min_ != 0)
    {
        (void)std::snprintf(range_buf, sizeof(range_buf), "%d\u00B0 / %d\u00B0",
                            static_cast<int>(temp_max_),
                            static_cast<int>(temp_min_));
    }
    else
    {
        std::strncpy(range_buf, "--", sizeof(range_buf));
    }
    const core::Rect range_box{
        body.x, static_cast<int16_t>(desc_box.bottom() + 8), body.w, 18};
    ts.size_px = Theme::kFontBody;
    char hi_lo[40];
    (void)std::snprintf(hi_lo, sizeof(hi_lo), "%s  %s", ui::strings::kWxHighLow, range_buf);
    c.text_in(range_box, hi_lo, ts);

    const core::Rect hint_box{
        body.x, static_cast<int16_t>(body.bottom() - 24), body.w, 20};
    ts.size_px = Theme::kFontSmall;
    c.text_in(hint_box, ui::strings::kWxTapRefresh, ts);
}

void WeatherApp::paint_city_pick(gfx::Canvas& c)
{
    city_list_.set_total(WeatherCities::count());
    city_list_.paint(c);
}

void WeatherApp::paint(gfx::Canvas& c)
{
    ui::widgets::toolbar(c, title());
    if (view_ == View::CityPick) paint_city_pick(c);
    else                         paint_main(c);
}

shell::InputResult WeatherApp::handle_main(int16_t x, int16_t y)
{
    if (city_switch_rect().contains(x, y))
    {
        view_ = View::CityPick;
        city_list_.set_scroll(0);
        request_repaint();
        return {true};
    }

    if (!fetching_ &&
        core::Rect{0, static_cast<int16_t>(Theme::kListStartY),
                   Theme::kScreenW,
                   static_cast<uint16_t>(Theme::kScreenH - Theme::kListStartY)}
            .contains(x, y))
    {
        start_fetch();
        return {true};
    }
    return {};
}

shell::InputResult WeatherApp::handle_city_pick(int16_t x, int16_t y)
{
    const core::Rect area = Theme::list_region();
    if (!area.contains(x, y)) return {};

    const uint8_t visible = Theme::kListVisibleRows;
    const uint8_t local   = static_cast<uint8_t>((y - area.y) / Theme::kListRowH);
    if (local >= visible) return {};

    const uint8_t idx = static_cast<uint8_t>(city_list_.scroll() + local);
    if (idx >= WeatherCities::count()) return {};

    if (idx != city_idx_)
    {
        city_idx_ = idx;
        WeatherCities::save_index(city_idx_);
    }

    ip_locate_next_ = false;
    view_           = View::Main;
    start_fetch();
    return {true};
}

shell::InputResult WeatherApp::on_input(const ::app::ebook::input::Event& ev)
{
    using ::app::ebook::input::EventType;

    if (ev.type == EventType::Tap)
    {
        const int16_t x = static_cast<int16_t>(ev.x);
        const int16_t y = static_cast<int16_t>(ev.y);

        if (ui::widgets::hit_toolbar_back(x, y))
        {
            if (view_ == View::CityPick)
            {
                view_ = View::Main;
                request_repaint();
                return {true};
            }
            return {};
        }

        if (view_ == View::CityPick)
        {
            auto out = city_list_.handle_input(ev);
            if (out.scroll_changed) request_repaint();
            return handle_city_pick(x, y);
        }
        return handle_main(x, y);
    }

    if (view_ == View::CityPick)
    {
        auto out = city_list_.handle_input(ev);
        if (out.scroll_changed) request_repaint();
        if (out.consumed) return {true};
    }
    return {};
}

} // namespace app::ebook::apps::weather
