#include "apps/clock/clock_app.hpp"

#include <cstdio>
#include <cstdlib>

#include "data/clock_provider.hpp"
#include "gfx/icon.hpp"
#include "overlays/keyboard.hpp"
#include "overlays/toast.hpp"
#include "ui/strings.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"

namespace app::ebook::apps::clock {

namespace {

using ui::Theme;
constexpr uint16_t kTabBarH      = 24;
constexpr uint16_t kContentTopY  = Theme::kListStartY + kTabBarH;
constexpr uint16_t kAlarmAddBtnH = 30;
constexpr uint8_t  kNavIconSize   = 14;
constexpr uint16_t kTimeStepBtnW  = 32;
constexpr uint16_t kTimeLblW      = 18;

struct TimeStepperGeom
{
    core::Rect row;
    core::Rect label;
    core::Rect dec;
    core::Rect val;
    core::Rect inc;
};

void draw_nav_btn(gfx::Canvas& c, const core::Rect& btn, uint32_t icon_cp)
{
    c.rect(btn, 1);
    const int16_t ax = static_cast<int16_t>(btn.x + (btn.w - kNavIconSize) / 2);
    const int16_t ay = static_cast<int16_t>(btn.y + (btn.h - kNavIconSize) / 2);
    c.glyph(ax, ay, icon_cp, kNavIconSize, gfx::FontFace::Icon);
}

int16_t alarm_edit_hour_y()
{
    return static_cast<int16_t>(kContentTopY + 4 + 24);
}

int16_t alarm_edit_minute_y()
{
    return static_cast<int16_t>(alarm_edit_hour_y() + Theme::kListRowH);
}

int16_t alarm_edit_repeat_y()
{
    return static_cast<int16_t>(alarm_edit_minute_y() + Theme::kListRowH + 4);
}

TimeStepperGeom time_stepper_geom(int16_t y)
{
    const core::Rect row{Theme::kPadLg, y,
                         static_cast<uint16_t>(Theme::kScreenW - Theme::kPadLg * 2),
                         Theme::kListRowH};
    const core::Rect label{row.x, row.y, kTimeLblW, row.h};
    const core::Rect dec{static_cast<int16_t>(label.right()), row.y,
                         kTimeStepBtnW, row.h};
    const core::Rect inc{static_cast<int16_t>(row.right() - kTimeStepBtnW),
                         row.y, kTimeStepBtnW, row.h};
    const core::Rect val{static_cast<int16_t>(dec.right()), row.y,
                         static_cast<uint16_t>(inc.x - dec.right()), row.h};
    return {row, label, dec, val, inc};
}

void paint_time_stepper(gfx::Canvas& c, const TimeStepperGeom& g,
                        const char* label, uint8_t value)
{
    gfx::Canvas::TextStyle ts{};
    ts.size_px = Theme::kFontBody;
    ts.v       = gfx::VAlign::Middle;
    c.text_in(g.label, label, ts);

    draw_nav_btn(c, g.dec, gfx::icon::kFaMinus);
    draw_nav_btn(c, g.inc, gfx::icon::kFaPlus);

    char buf[8];
    (void)std::snprintf(buf, sizeof(buf), "%02u", static_cast<unsigned>(value));
    c.rect(g.val, 1);
    ts.size_px = Theme::kFontTitle;
    ts.h       = gfx::HAlign::Center;
    c.text_in(g.val, buf, ts);
}

bool parse_time_field(const char* text, uint8_t max_val, uint8_t& out)
{
    if (text == nullptr || text[0] == '\0')
        return false;
    for (const char* p = text; *p != '\0'; ++p)
    {
        if (*p < '0' || *p > '9')
            return false;
    }
    char* end = nullptr;
    const long v = std::strtol(text, &end, 10);
    if (end == text || *end != '\0' || v < 0 || v > static_cast<long>(max_val))
        return false;
    out = static_cast<uint8_t>(v);
    return true;
}

} // namespace

ClockApp& ClockApp::instance()
{
    static ClockApp s;
    return s;
}

const char* ClockApp::title()   const { return ui::strings::kAppClock; }
uint32_t    ClockApp::icon_cp() const { return gfx::icon::kFaClock; }

/*============================================================================
 * 布局
 *============================================================================*/

core::Rect ClockApp::tabs_rect()
{
    return core::Rect{0, static_cast<int16_t>(Theme::kListStartY),
                      Theme::kScreenW, kTabBarH};
}

core::Rect ClockApp::content_rect()
{
    const int16_t y = static_cast<int16_t>(kContentTopY);
    return core::Rect{0, y, Theme::kScreenW, static_cast<uint16_t>(Theme::kScreenH - y)};
}

core::Rect ClockApp::alarm_add_btn_rect()
{
    const core::Rect c = content_rect();
    return core::Rect{c.x, static_cast<int16_t>(c.bottom() - kAlarmAddBtnH),
                      c.w, kAlarmAddBtnH};
}

/*============================================================================
 * 失效
 *============================================================================*/

void ClockApp::invalidate_body()
{
    request_repaint();
}

/*============================================================================
 * 生命周期
 *============================================================================*/

void ClockApp::set_tab(Tab t)
{
    tab_         = t;
    sub_         = SubPage::Main;
    edit_is_new_ = false;
    invalidate_body();
}

void ClockApp::cancel_alarm_edit()
{
    if (sub_ != SubPage::AlarmEdit) return;

    if (overlays::Keyboard::instance().is_open())
        overlays::Keyboard::instance().close();

    auto& st = ClockStore::get_instance();
    if (edit_is_new_)
    {
        if (edit_alarm_idx_ < st.alarm_count())
            st.remove_alarm(edit_alarm_idx_);
    }
    else if (edit_alarm_idx_ < st.alarm_count())
    {
        st.alarm_mut(edit_alarm_idx_) = edit_snapshot_;
    }

    edit_is_new_ = false;
    sub_         = SubPage::Main;
    tab_         = Tab::Alarm;
    invalidate_body();
}

void ClockApp::on_enter()
{
    tab_         = Tab::Clock;
    sub_         = SubPage::Main;
    edit_is_new_ = false;

    alarm_list_.set_area(content_rect());
    alarm_list_.set_provider([](uint8_t i, ui::widgets::RowStyle& s) {
        auto& st = ClockStore::get_instance();
        if (i >= st.alarm_count())
        {
            s.label = ui::strings::kEmpty;
            return;
        }
        const auto& a = st.alarm(i);
        static char buf[32];
        (void)std::snprintf(buf, sizeof(buf), "%02u:%02u %s",
                            static_cast<unsigned>(a.hour),
                            static_cast<unsigned>(a.minute),
                            st.repeat_label(a.repeat));
        s.label           = buf;
        s.value_icon      = gfx::icon::toggle(a.enabled);
        s.value_icon_size = 20;
        s.show_chevron    = true;
    });

    invalidate_body();
}

void ClockApp::on_exit()
{
    if (overlays::Keyboard::instance().is_open())
        overlays::Keyboard::instance().close();
    sub_ = SubPage::Main;
}

void ClockApp::open_hour_keyboard()
{
    auto& st = ClockStore::get_instance();
    if (edit_alarm_idx_ >= st.alarm_count())
        return;

    const auto& a = st.alarm(edit_alarm_idx_);
    char init[4];
    (void)std::snprintf(init, sizeof(init), "%02u", static_cast<unsigned>(a.hour));

    overlays::KeyboardConfig kc{};
    kc.default_layer = overlays::KeyboardLayer::Numbers;
    kc.max_len       = 2;
    kc.initial_text  = init;
    kc.title         = ui::strings::kClkHourHint;
    overlays::Keyboard::instance().open(kc, on_hour_kb_done, this);
}

void ClockApp::open_minute_keyboard()
{
    auto& st = ClockStore::get_instance();
    if (edit_alarm_idx_ >= st.alarm_count())
        return;

    const auto& a = st.alarm(edit_alarm_idx_);
    char init[4];
    (void)std::snprintf(init, sizeof(init), "%02u", static_cast<unsigned>(a.minute));

    overlays::KeyboardConfig kc{};
    kc.default_layer = overlays::KeyboardLayer::Numbers;
    kc.max_len       = 2;
    kc.initial_text  = init;
    kc.title         = ui::strings::kClkMinuteHint;
    overlays::Keyboard::instance().open(kc, on_minute_kb_done, this);
}

void ClockApp::on_hour_kb_done(const char* text, void* user)
{
    auto* self = static_cast<ClockApp*>(user);
    if (self == nullptr)
        return;

    auto& st = ClockStore::get_instance();
    if (self->edit_alarm_idx_ >= st.alarm_count())
        return;

    if (text == nullptr || text[0] == '\0')
    {
        overlays::Toast::instance().show(ui::strings::kClkTimeEmpty, 1500);
        self->request_repaint();
        return;
    }

    uint8_t hour = 0;
    if (!parse_time_field(text, 23, hour))
    {
        overlays::Toast::instance().show(ui::strings::kClkTimeInvalid, 1500);
        self->request_repaint();
        return;
    }

    st.alarm_mut(self->edit_alarm_idx_).hour = hour;
    self->request_repaint();
}

void ClockApp::on_minute_kb_done(const char* text, void* user)
{
    auto* self = static_cast<ClockApp*>(user);
    if (self == nullptr)
        return;

    auto& st = ClockStore::get_instance();
    if (self->edit_alarm_idx_ >= st.alarm_count())
        return;

    if (text == nullptr || text[0] == '\0')
    {
        overlays::Toast::instance().show(ui::strings::kClkTimeEmpty, 1500);
        self->request_repaint();
        return;
    }

    uint8_t minute = 0;
    if (!parse_time_field(text, 59, minute))
    {
        overlays::Toast::instance().show(ui::strings::kClkTimeInvalid, 1500);
        self->request_repaint();
        return;
    }

    st.alarm_mut(self->edit_alarm_idx_).minute = minute;
    self->request_repaint();
}

void ClockApp::on_ui_event(const ui::UiEvent& ev)
{
    if (ev.kind == ui::UiEventKind::SystemHint &&
        ev.payload.system.hint == ui::SystemHintKind::ClockAlarm)
    {
        request_repaint();
    }
}

/*============================================================================
 * paint
 *============================================================================*/

void ClockApp::paint_tabs(gfx::Canvas& c)
{
    const core::Rect bar = tabs_rect();
    c.hline(bar.x, static_cast<int16_t>(bar.bottom() - 1), bar.w);

    static const char* const kLabels[] = {
        ui::strings::kClkTabClock,
        ui::strings::kClkTabAlarm,
    };

    const uint16_t tw = static_cast<uint16_t>(Theme::kScreenW / 2);
    gfx::Canvas::TextStyle ts{};
    ts.size_px = Theme::kFontSmall;
    ts.h       = gfx::HAlign::Center;
    ts.v       = gfx::VAlign::Middle;

    for (uint8_t i = 0; i < 2; ++i)
    {
        const int16_t x = static_cast<int16_t>(i * tw);
        const core::Rect cell{x, bar.y, tw, bar.h};
        const bool active = (static_cast<uint8_t>(tab_) == i);
        if (active) c.fill(cell, gfx::Ink::Black);
        ts.ink = active ? gfx::Ink::White : gfx::Ink::Black;
        c.text_in(cell, kLabels[i], ts);
        if (i > 0) c.vline(x, bar.y, bar.h);
    }
}

void ClockApp::paint_clock_face(gfx::Canvas& c)
{
    const core::Rect area = content_rect();
    const auto clk = data::Clock::now();

    char hm[8];
    clk.format_time_hm(hm, sizeof(hm));

    const core::Rect time_box{area.x, static_cast<int16_t>(area.y + 24), area.w, 40};
    gfx::Canvas::TextStyle ts{};
    ts.size_px = Theme::kFontHuge;
    ts.h       = gfx::HAlign::Center;
    ts.v       = gfx::VAlign::Middle;
    c.text_in(time_box, hm, ts);

    char date_buf[32];
    clk.format_date_cn(date_buf, sizeof(date_buf));
    char line[40];
    (void)std::snprintf(line, sizeof(line), "%s  %s", date_buf, clk.weekday_name_cn());

    const core::Rect date_box{
        area.x, static_cast<int16_t>(time_box.bottom() + 4), area.w, 20};
    ts.size_px = Theme::kFontBody;
    c.text_in(date_box, line, ts);
}

void ClockApp::paint_alarm_list(gfx::Canvas& c)
{
    auto& st = ClockStore::get_instance();
    const core::Rect add_btn = alarm_add_btn_rect();
    core::Rect list_area = content_rect();
    list_area.h = static_cast<uint16_t>(list_area.h - kAlarmAddBtnH);

    alarm_list_.set_area(list_area);
    alarm_list_.set_total(st.alarm_count());

    if (st.alarm_count() == 0)
    {
        gfx::Canvas::TextStyle ts{};
        ts.size_px = Theme::kFontBody;
        ts.h       = gfx::HAlign::Center;
        ts.v       = gfx::VAlign::Middle;
        c.text_in(list_area, ui::strings::kClkNoAlarm, ts);
    }
    else
    {
        alarm_list_.paint(c);
    }

    c.hline(add_btn.x, add_btn.y, add_btn.w);
    gfx::Canvas::TextStyle ts{};
    ts.size_px = Theme::kFontBody;
    ts.h       = gfx::HAlign::Center;
    ts.v       = gfx::VAlign::Middle;
    c.text_in(add_btn, ui::strings::kClkAddAlarm, ts);
}

void ClockApp::paint_alarm_edit(gfx::Canvas& c)
{
    auto& st = ClockStore::get_instance();
    if (edit_alarm_idx_ >= st.alarm_count()) return;

    const auto& a = st.alarm(edit_alarm_idx_);
    int16_t y = static_cast<int16_t>(kContentTopY + 4);

    gfx::Canvas::TextStyle ts{};
    ts.size_px = Theme::kFontTitle;
    ts.h       = gfx::HAlign::Center;
    const core::Rect hdr{0, y, Theme::kScreenW, 20};
    c.text_in(hdr, ui::strings::kClkEditAlarm, ts);

    const TimeStepperGeom hour_g = time_stepper_geom(alarm_edit_hour_y());
    const TimeStepperGeom min_g  = time_stepper_geom(alarm_edit_minute_y());
    paint_time_stepper(c, hour_g, ui::strings::kClkHourLabel, a.hour);
    paint_time_stepper(c, min_g, ui::strings::kClkMinuteLabel, a.minute);

    const core::Rect row = hour_g.row;
    y = alarm_edit_repeat_y();

    ui::widgets::RowStyle rs{};
    rs.label        = ui::strings::kClkRepeat;
    rs.value        = st.repeat_label(a.repeat);
    rs.show_chevron = true;
    ui::widgets::list_row(c, core::Rect{row.x, y, row.w, Theme::kListRowH}, rs);
    y = static_cast<int16_t>(y + Theme::kListRowH);

    rs = {};
    rs.label           = ui::strings::kOn;
    rs.value_icon      = gfx::icon::toggle(a.enabled);
    rs.value_icon_size = 20;
    rs.show_chevron    = false;
    ui::widgets::list_row(c, core::Rect{row.x, y, row.w, Theme::kListRowH}, rs);
    y = static_cast<int16_t>(y + Theme::kListRowH);

    if (!edit_is_new_)
    {
        rs = {};
        rs.label        = ui::strings::kClkDelete;
        rs.show_chevron = false;
        ui::widgets::list_row(c, core::Rect{row.x, y, row.w, Theme::kListRowH}, rs);
        y = static_cast<int16_t>(y + Theme::kListRowH);
    }

    y = static_cast<int16_t>(y + 8);
    const core::Rect ok_btn{row.x, y, row.w, 28};
    c.rect(ok_btn, 1);
    ts.size_px = Theme::kFontBody;
    c.text_in(ok_btn, ui::strings::kOk, ts);
}

void ClockApp::paint(gfx::Canvas& c)
{
    ui::widgets::toolbar(c, title());
    paint_tabs(c);

    if (sub_ == SubPage::AlarmEdit) { paint_alarm_edit(c); return; }

    if (tab_ == Tab::Clock) paint_clock_face(c);
    else                    paint_alarm_list(c);
}

/*============================================================================
 * 输入
 *============================================================================*/

shell::InputResult ClockApp::handle_tabs(int16_t x, int16_t y)
{
    const core::Rect bar = tabs_rect();
    if (!bar.contains(x, y)) return {};

    const uint16_t tw = static_cast<uint16_t>(Theme::kScreenW / 2);
    const uint8_t  idx = static_cast<uint8_t>(x / tw);
    if (idx > 1) return {};
    set_tab(static_cast<Tab>(idx));
    return {true};
}

shell::InputResult ClockApp::handle_alarm_list(int16_t x, int16_t y)
{
    auto& st = ClockStore::get_instance();

    if (alarm_add_btn_rect().contains(x, y))
    {
        if (st.add_alarm())
        {
            edit_alarm_idx_ = static_cast<uint8_t>(st.alarm_count() - 1);
            edit_is_new_    = true;
            sub_            = SubPage::AlarmEdit;
            invalidate_body();
        }
        else
        {
            overlays::Toast::instance().show(
                "\u6700\u591A 6 \u4E2A\u95F9\u949F", 1500);
        }
        return {true};
    }

    core::Rect list_area = content_rect();
    list_area.h = static_cast<uint16_t>(list_area.h - kAlarmAddBtnH);
    if (!list_area.contains(x, y)) return {};

    const uint8_t visible = static_cast<uint8_t>(list_area.h / Theme::kListRowH);
    const uint8_t local   = static_cast<uint8_t>((y - list_area.y) / Theme::kListRowH);
    if (local >= visible) return {};

    const uint8_t idx = static_cast<uint8_t>(alarm_list_.scroll() + local);
    if (idx >= st.alarm_count()) return {};

    const core::Rect row{
        list_area.x, static_cast<int16_t>(list_area.y + local * Theme::kListRowH),
        static_cast<uint16_t>(list_area.w - Theme::kScrollbarW - 2),
        Theme::kListRowH};

    if (ui::widgets::hit_row_action(row, x, y))
    {
        auto& a   = st.alarm_mut(idx);
        a.enabled = !a.enabled;
        st.save();
        invalidate_body();
        return {true};
    }

    edit_alarm_idx_ = idx;
    edit_is_new_    = false;
    edit_snapshot_  = st.alarm(idx);
    sub_            = SubPage::AlarmEdit;
    invalidate_body();
    return {true};
}

shell::InputResult ClockApp::handle_alarm_edit(int16_t x, int16_t y)
{
    auto& st = ClockStore::get_instance();
    if (edit_alarm_idx_ >= st.alarm_count()) return {true};

    auto& a = st.alarm_mut(edit_alarm_idx_);
    const TimeStepperGeom hour_g = time_stepper_geom(alarm_edit_hour_y());
    const TimeStepperGeom min_g  = time_stepper_geom(alarm_edit_minute_y());
    const core::Rect      row    = hour_g.row;

    if (hour_g.dec.contains(x, y))
    {
        a.hour = (a.hour > 0) ? static_cast<uint8_t>(a.hour - 1U) : 23U;
        request_repaint();
        return {true};
    }
    if (hour_g.inc.contains(x, y))
    {
        a.hour = (a.hour < 23) ? static_cast<uint8_t>(a.hour + 1U) : 0U;
        request_repaint();
        return {true};
    }
    if (hour_g.val.contains(x, y))
    {
        open_hour_keyboard();
        return {true};
    }

    if (min_g.dec.contains(x, y))
    {
        a.minute = (a.minute > 0) ? static_cast<uint8_t>(a.minute - 1U) : 59U;
        request_repaint();
        return {true};
    }
    if (min_g.inc.contains(x, y))
    {
        a.minute = (a.minute < 59) ? static_cast<uint8_t>(a.minute + 1U) : 0U;
        request_repaint();
        return {true};
    }
    if (min_g.val.contains(x, y))
    {
        open_minute_keyboard();
        return {true};
    }

    int16_t ry = alarm_edit_repeat_y();
    const core::Rect rep_row{row.x, ry, row.w, Theme::kListRowH};
    if (rep_row.contains(x, y))
    {
        a.repeat = static_cast<AlarmRepeat>(
            (static_cast<uint8_t>(a.repeat) + 1U) % 3U);
        invalidate_body();
        return {true};
    }

    ry = static_cast<int16_t>(ry + Theme::kListRowH);
    const core::Rect en_row{row.x, ry, row.w, Theme::kListRowH};
    if (en_row.contains(x, y))
    {
        a.enabled = !a.enabled;
        invalidate_body();
        return {true};
    }

    ry = static_cast<int16_t>(ry + Theme::kListRowH);
    if (!edit_is_new_)
    {
        const core::Rect del_row{row.x, ry, row.w, Theme::kListRowH};
        if (del_row.contains(x, y))
        {
            st.remove_alarm(edit_alarm_idx_);
            sub_ = SubPage::Main;
            invalidate_body();
            return {true};
        }
        ry = static_cast<int16_t>(ry + Theme::kListRowH);
    }

    ry = static_cast<int16_t>(ry + 8);
    const core::Rect ok_btn{row.x, ry, row.w, 28};
    if (ok_btn.contains(x, y))
    {
        st.save();
        edit_is_new_ = false;
        sub_         = SubPage::Main;
        invalidate_body();
        return {true};
    }
    return {true};
}

shell::InputResult ClockApp::on_input(const ::app::ebook::input::Event& ev)
{
    using ::app::ebook::input::EventType;

    if (ev.type == EventType::Tap)
    {
        const int16_t x = static_cast<int16_t>(ev.x);
        const int16_t y = static_cast<int16_t>(ev.y);

        if (ui::widgets::hit_toolbar_back(x, y))
        {
            if (sub_ == SubPage::AlarmEdit) { cancel_alarm_edit(); return {true}; }
            return {};
        }

        if (sub_ == SubPage::AlarmEdit) return handle_alarm_edit(x, y);

        if (auto r = handle_tabs(x, y); r.consumed) return r;

        if (tab_ == Tab::Alarm)
        {
            auto out = alarm_list_.handle_input(ev);
            if (out.scroll_changed) invalidate_body();
            if (out.consumed && !out.tap_consumed) return {true};
            return handle_alarm_list(x, y);
        }
    }

    if (sub_ == SubPage::Main && tab_ == Tab::Alarm)
    {
        auto out = alarm_list_.handle_input(ev);
        if (out.scroll_changed) invalidate_body();
        if (out.consumed) return {true};
    }
    return {};
}

} // namespace app::ebook::apps::clock
