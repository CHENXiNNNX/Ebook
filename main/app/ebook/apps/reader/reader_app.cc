#include "apps/reader/reader_app.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>

#include "apps/reader/reader_menu.hpp"
#include "data/clock_provider.hpp"
#include "data/persist.hpp"
#include "apps/reader/reading_store.hpp"
#include "apps/reader/txt_index_cache.hpp"
#include "data/system_state.hpp"
#include "gfx/icon.hpp"
#include "gfx/text_layout.hpp"
#include "overlays/keyboard.hpp"
#include "overlays/toast.hpp"
#include "router/refresh_intent.hpp"
#include "router/router.hpp"
#include "text/path_encoding.hpp"
#include "ui/strings.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"

namespace app::ebook::apps::reader {

namespace {

using ui::Theme;

constexpr const char* kNvsFont    = "reader.font";
constexpr const char* kNvsRefresh = "reader.refresh";

constexpr uint16_t kClearFullEvery  = 20;
constexpr uint16_t kNormalFastEvery = 10;
constexpr uint16_t kNormalFullEvery = 30;

} // namespace

bool ReaderApp::s_resume_on_enter_ = false;
bool ReaderApp::s_open_on_enter_   = false;
char ReaderApp::s_open_path_[96]{};

ReaderApp& ReaderApp::instance()
{
    static ReaderApp s;
    return s;
}

void ReaderApp::request_resume_on_enter()
{
    s_resume_on_enter_ = true;
}

void ReaderApp::request_open_on_enter(const char* path)
{
    if (path == nullptr || path[0] == '\0')
        return;
    (void)std::strncpy(s_open_path_, path, sizeof(s_open_path_) - 1);
    s_open_path_[sizeof(s_open_path_) - 1] = '\0';
    s_open_on_enter_ = true;
}

ReaderApp::ReaderApp()
{
    list_.set_area(Theme::list_region());
    list_.set_provider([this](uint8_t i, ui::widgets::RowStyle& rs) { fill_shelf_row(i, rs); });
    list_.set_tap_handler([this](uint8_t i) { on_shelf_tap(i); });
}

const char* ReaderApp::title() const
{
    if (screen_ == Screen::Reading && open_title_[0] != '\0')
    {
        char norm[48];
        (void)text::normalize_path_segment(open_title_, norm, sizeof(norm));
        (void)gfx::truncate_text(norm, Theme::kFontBody,
                                 static_cast<uint16_t>(Theme::kScreenW - Theme::kPadLg * 2),
                                 title_buf_, sizeof(title_buf_));
        return title_buf_;
    }
    return ui::strings::kAppReader;
}

uint32_t ReaderApp::icon_cp() const
{
    return gfx::icon::kFaBookReader;
}

bool ReaderApp::wants_status_bar() const
{
    return screen_ == Screen::Shelf;
}

ReaderLayout ReaderApp::layout() const
{
    return ReaderLayout(ReaderLayout::kFontSizes[font_scale_ % 3U]);
}

const char* ReaderApp::font_label() const
{
    switch (font_scale_ % 3U)
    {
        case 0:  return "\u5C0F";
        case 2:  return "\u5927";
        default: return "\u4E2D";
    }
}

const char* ReaderApp::refresh_label() const
{
    switch (static_cast<ReadRefreshMode>(refresh_mode_))
    {
        case ReadRefreshMode::Clear: return "\u6E05\u6670";
        case ReadRefreshMode::Fast:  return "\u5FEB\u901F";
        default:                     return "\u666E\u901A";
    }
}

void ReaderApp::on_enter()
{
    uint8_t saved_scale = 1;
    if (data::Persist::get_u8(kNvsFont, saved_scale) && saved_scale < 3U)
        font_scale_ = saved_scale;

    uint8_t saved_refresh = static_cast<uint8_t>(ReadRefreshMode::Normal);
    if (data::Persist::get_u8(kNvsRefresh, saved_refresh) && saved_refresh < 3U)
        refresh_mode_ = saved_refresh;

    screen_       = Screen::Shelf;
    menu_visible_ = false;
    book_.close();
    open_title_[0] = '\0';
    open_path_[0]  = '\0';
    tick_hour_     = 0xFF;
    tick_minute_   = 0xFF;
    tick_battery_  = 0xFF;
    turn_count_ = 0;

    shelf_.scan();
    list_.set_scroll(0);
    list_.set_total(shelf_.count());

    if (s_resume_on_enter_)
    {
        s_resume_on_enter_ = false;
        (void)try_resume_saved_book();
    }
    else if (s_open_on_enter_)
    {
        s_open_on_enter_ = false;
        (void)open_book_by_path(s_open_path_);
    }
}

void ReaderApp::on_exit()
{
    if (overlays::Keyboard::instance().is_open())
        overlays::Keyboard::instance().close();
    save_progress();
    book_.close();
    screen_       = Screen::Shelf;
    menu_visible_ = false;
}

void ReaderApp::save_progress()
{
    if (screen_ != Screen::Reading || !book_.is_open() || open_path_[0] == '\0')
        return;

    (void)TxtIndexCache::save_progress(
        open_path_, book_.page(), book_.page_count());

    ReadingStore::get_instance().set_book(
        open_title_, open_path_, book_.page(), book_.page_count());
}

void ReaderApp::restore_progress()
{
    uint16_t saved = 0;
    if (open_path_[0] != '\0' &&
        TxtIndexCache::load_progress(open_path_, &saved))
    {
        if (saved < book_.page_count())
            (void)book_.set_page(saved);
        return;
    }

    auto& store = ReadingStore::get_instance();
    if (store.has_book() && std::strcmp(store.book_path(), open_path_) == 0 &&
        store.total_pages() == book_.page_count())
    {
        const uint16_t p = store.current_page();
        if (p < book_.page_count())
            (void)book_.set_page(p);
    }
}

void ReaderApp::return_to_shelf()
{
    save_progress();
    book_.close();
    open_title_[0] = '\0';
    open_path_[0]  = '\0';
    screen_        = Screen::Shelf;
    menu_visible_  = false;
    shelf_.scan();
    list_.set_scroll(0);
    list_.set_total(shelf_.count());
    request_repaint();
}

void ReaderApp::toggle_reading_menu()
{
    if (screen_ != Screen::Reading)
        return;
    menu_visible_ = !menu_visible_;
    request_repaint();
}

bool ReaderApp::on_semantic_back()
{
    if (screen_ == Screen::Opening)
        return true;
    if (menu_visible_)
    {
        menu_visible_ = false;
        request_repaint();
        return true;
    }
    if (screen_ == Screen::Reading)
    {
        return_to_shelf();
        return true;
    }
    return false;
}

void ReaderApp::go_lock_screen()
{
    save_progress();
    (void)router::Router::instance().replace_shell(router::ShellPage::Lock);
}

void ReaderApp::after_page_turn()
{
    save_progress();
    ++turn_count_;

    switch (static_cast<ReadRefreshMode>(refresh_mode_))
    {
        case ReadRefreshMode::Clear:
            if (turn_count_ % kClearFullEvery == 0)
                request_repaint(router::intent_full_full());
            else
                request_repaint(router::intent_fast_full());
            break;

        case ReadRefreshMode::Normal:
            if (turn_count_ % kNormalFullEvery == 0)
                request_repaint(router::intent_full_full());
            else if (turn_count_ % kNormalFastEvery == 0)
                request_repaint(router::intent_fast_full());
            else
                request_repaint();
            break;

        default:
            request_repaint();
            break;
    }
}

bool ReaderApp::open_book_by_path(const char* path)
{
    if (path == nullptr || path[0] == '\0')
        return false;

    struct stat st{};
    if (::stat(path, &st) != 0 || S_ISDIR(st.st_mode))
        return false;

    const char* base = std::strrchr(path, '/');
    base = (base != nullptr) ? (base + 1) : path;

    char title[sizeof(open_title_)];
    title[0] = '\0';
    {
        char raw[48];
        const size_t flen = std::strlen(base);
        size_t copy_len   = flen;
        if (flen >= 4)
            copy_len = flen - 4U;
        if (copy_len >= sizeof(raw))
            copy_len = sizeof(raw) - 1U;
        std::memcpy(raw, base, copy_len);
        raw[copy_len] = '\0';
        (void)text::normalize_path_segment(raw, title, sizeof(title));
        if (title[0] == '\0')
        {
            const size_t n = (copy_len < sizeof(title) - 1) ? copy_len : (sizeof(title) - 1);
            std::memcpy(title, raw, n);
            title[n] = '\0';
        }
    }

    (void)std::strncpy(open_title_, title, sizeof(open_title_) - 1);
    open_title_[sizeof(open_title_) - 1] = '\0';
    (void)std::strncpy(open_path_, path, sizeof(open_path_) - 1);
    open_path_[sizeof(open_path_) - 1] = '\0';

    if (static_cast<uint32_t>(st.st_size) > TxtBook::kMaxFileBytes)
    {
        overlays::Toast::instance().show(ui::strings::kReaderTooLarge, 2000);
        return false;
    }

    screen_       = Screen::Opening;
    menu_visible_ = false;
    request_repaint();

    const ReaderLayout ly = layout();
    if (!book_.open(open_path_, ly))
    {
        screen_ = Screen::Shelf;
        overlays::Toast::instance().show(ui::strings::kReaderOpenFail, 2000);
        request_repaint();
        return false;
    }

    restore_progress();
    turn_count_ = 0;
    screen_     = Screen::Reading;
    save_progress();
    request_repaint();
    return true;
}

bool ReaderApp::open_book(uint8_t shelf_idx)
{
    const BookItem& item = shelf_.item(shelf_idx);
    if (item.path[0] == '\0')
        return false;
    return open_book_by_path(item.path);
}

bool ReaderApp::try_resume_saved_book()
{
    auto& store = ReadingStore::get_instance();
    if (!store.has_book())
        return false;

    const char* path = store.book_path();
    if (path == nullptr || path[0] == '\0')
        return false;

    for (uint8_t i = 0; i < shelf_.count(); ++i)
    {
        if (std::strcmp(shelf_.item(i).path, path) == 0)
            return open_book(i);
    }
    return open_book_by_path(path);
}

void ReaderApp::fill_shelf_row(uint8_t idx, ui::widgets::RowStyle& rs)
{
    rs = {};
    const BookItem& item = shelf_.item(idx);
    if (item.path[0] == '\0')
        return;

    if (item.title[0] == '\0')
        (void)std::strncpy(row_title_, "?", sizeof(row_title_) - 1);
    else
        (void)gfx::truncate_text(item.title, Theme::kFontBody,
                                 static_cast<uint16_t>(Theme::kScreenW - 80),
                                 row_title_, sizeof(row_title_));
    row_title_[sizeof(row_title_) - 1] = '\0';
    rs.label    = row_title_;
    rs.icon_cp  = gfx::icon::kFaBookReader;

    uint8_t pct = 0;
    if (TxtIndexCache::query_read_percent(item.path, &pct) && pct > 0)
    {
        (void)std::snprintf(row_value_, sizeof(row_value_), ui::strings::kReaderReadFmt,
                       static_cast<unsigned>(pct));
        rs.value = row_value_;
    }
    else
    {
        gfx::format_file_size(item.size_bytes, row_value_, sizeof(row_value_));
        rs.value = row_value_;
    }
    rs.show_chevron = true;
}

void ReaderApp::on_shelf_tap(uint8_t idx)
{
    (void)open_book(idx);
}

void ReaderApp::paint_shelf(gfx::Canvas& c)
{
    ui::widgets::toolbar(c, ui::strings::kAppReader);
    if (shelf_.count() == 0)
    {
        static const char* const kHints[] = {
            BookShelf::kScanFmtHint,
            BookShelf::kScanPathInt,
            BookShelf::kScanPathSd,
        };
        ui::widgets::empty_state(c, Theme::list_region(), ui::strings::kReaderShelfEmpty, kHints, 2);
        return;
    }
    list_.paint(c);
}

void ReaderApp::paint_opening(gfx::Canvas& c)
{
    gfx::Canvas::TextStyle ts{};
    ts.size_px = Theme::kFontBody;
    ts.h       = gfx::HAlign::Center;
    ts.v       = gfx::VAlign::Middle;
    c.text_in(layout().content_rect(), ui::strings::kReaderOpening, ts);
}

void ReaderApp::paint_top_bar(gfx::Canvas& c)
{
    const ReaderLayout ly = layout();
    const core::Rect bar = ly.top_bar_rect();
    c.fill(bar, gfx::Ink::White);

    auto clk = data::Clock::now();
    char time_buf[8];
    clk.format_time_hm(time_buf, sizeof(time_buf));

    const int16_t ty = static_cast<int16_t>((ReaderLayout::kTopBarH - Theme::kFontSmall) / 2);
    c.text(Theme::kPad, ty, time_buf, Theme::kFontSmall);

    char page_str[16];
    (void)std::snprintf(page_str, sizeof(page_str), "%u/%u",
                   static_cast<unsigned>(book_.page() + 1U),
                   static_cast<unsigned>(book_.page_count() > 0 ? book_.page_count() : 1U));

    const uint8_t bat = data::SystemState::get_instance().battery_pct();
    (void)ui::widgets::battery_indicator(
        c, static_cast<int16_t>(Theme::kScreenW - Theme::kPad), 0, bat,
        ReaderLayout::kTopBarH);

    const uint16_t pw = gfx::Font::get_instance().measure(page_str, Theme::kFontSmall);
    const int16_t  px = static_cast<int16_t>((Theme::kScreenW - pw) / 2);
    c.text(px, ty, page_str, Theme::kFontSmall);

    c.hline(0, static_cast<int16_t>(ReaderLayout::kTopBarH - 1), Theme::kScreenW);
}

void ReaderApp::paint_reading(gfx::Canvas& c)
{
    const ReaderLayout ly = layout();
    paint_top_bar(c);
    book_.paint_page(c, ly.content_rect());
    if (menu_visible_)
    {
        uint8_t pct = 0;
        if (book_.page_count() > 0)
        {
            const uint32_t raw =
                (static_cast<uint32_t>(book_.page() + 1U) * 100U) /
                static_cast<uint32_t>(book_.page_count());
            pct = static_cast<uint8_t>(raw > 100U ? 100U : raw);
        }
        MenuPaintInfo info{font_label(), refresh_label(), pct};
        paint_menu_panel(c, ly, info);
    }
}

void ReaderApp::paint(gfx::Canvas& c)
{
    switch (screen_)
    {
        case Screen::Shelf:
            paint_shelf(c);
            break;
        case Screen::Opening:
            paint_opening(c);
            break;
        case Screen::Reading:
            paint_reading(c);
            break;
    }
}

void ReaderApp::on_ui_event(const ui::UiEvent& ev)
{
    if (screen_ != Screen::Reading)
        return;

    if (ev.kind == ui::UiEventKind::TickClock)
    {
        if (ev.payload.clock.hour == tick_hour_ &&
            ev.payload.clock.minute == tick_minute_)
            return;
        tick_hour_   = ev.payload.clock.hour;
        tick_minute_ = ev.payload.clock.minute;
        request_repaint();
        return;
    }

    if (ev.kind == ui::UiEventKind::TickBattery)
    {
        if (ev.payload.battery.pct == tick_battery_)
            return;
        tick_battery_ = ev.payload.battery.pct;
        request_repaint();
    }
}

bool ReaderApp::in_reading_content(int16_t x, int16_t y) const
{
    const ReaderLayout ly = layout();
    if (y < ReaderLayout::kTopBarH)
        return false;
    if (menu_visible_ && y >= ly.menu_panel_y())
        return false;
    (void)x;
    return true;
}

void ReaderApp::go_next_page()
{
    if (!book_.next_page())
        return;
    after_page_turn();
}

void ReaderApp::go_prev_page()
{
    if (!book_.prev_page())
        return;
    after_page_turn();
}

void ReaderApp::go_page_relative(int delta)
{
    if (delta == 0 || book_.page_count() == 0)
        return;
    int32_t np = static_cast<int32_t>(book_.page()) + delta;
    if (np < 0)
        np = 0;
    if (np >= book_.page_count())
        np = static_cast<int32_t>(book_.page_count()) - 1;
    go_to_page(static_cast<uint16_t>(np));
}

void ReaderApp::go_to_page(uint16_t page_index)
{
    if (book_.page_count() == 0 || page_index >= book_.page_count())
        return;
    if (!book_.set_page(page_index))
        return;
    after_page_turn();
}

void ReaderApp::cycle_font_scale(int dir)
{
    const int next = static_cast<int>(font_scale_) + dir;
    font_scale_ = static_cast<uint8_t>((next % 3 + 3) % 3);
    data::Persist::set_u8(kNvsFont, font_scale_);
    data::Persist::commit();

    char msg[24];
    (void)std::snprintf(msg, sizeof(msg), "\u5B57\u53F7\uFF1A%s", font_label());
    overlays::Toast::instance().show(msg, 1200);
    apply_font_change();
}

void ReaderApp::apply_font_change()
{
    if (!book_.is_open())
        return;
    save_progress();
    const ReaderLayout ly = layout();
    (void)book_.set_layout(ly);
    restore_progress();
    request_repaint();
}

void ReaderApp::cycle_refresh_mode(int dir)
{
    const int next = static_cast<int>(refresh_mode_) + dir;
    refresh_mode_ = static_cast<uint8_t>((next % 3 + 3) % 3);
    data::Persist::set_u8(kNvsRefresh, refresh_mode_);
    data::Persist::commit();
    turn_count_ = 0;

    char msg[32];
    (void)std::snprintf(msg, sizeof(msg), "\u5237\u65B0\uFF1A%s", refresh_label());
    overlays::Toast::instance().show(msg, 1200);
    request_repaint();
}

void ReaderApp::open_page_jump()
{
    if (book_.page_count() == 0)
        return;

    char init[8];
    (void)std::snprintf(init, sizeof(init), "%u",
                   static_cast<unsigned>(book_.page() + 1U));
    (void)std::snprintf(jump_kb_title_, sizeof(jump_kb_title_),
                   "\u9875\u7801 1-%u",
                   static_cast<unsigned>(book_.page_count()));

    overlays::KeyboardConfig kc{};
    kc.default_layer = overlays::KeyboardLayer::Numbers;
    kc.max_len       = 5;
    kc.initial_text  = init;
    kc.title         = jump_kb_title_;
    overlays::Keyboard::instance().open(kc, on_page_jump_done, this);
    menu_visible_ = false;
}

void ReaderApp::on_page_jump_done(const char* text, void* user)
{
    auto* self = static_cast<ReaderApp*>(user);
    if (self == nullptr)
        return;

    if (text == nullptr || text[0] == '\0')
    {
        overlays::Toast::instance().show(ui::strings::kReaderJumpEmpty, 1500);
        self->request_repaint();
        return;
    }

    for (const char* p = text; *p != '\0'; ++p)
    {
        if (*p < '0' || *p > '9')
        {
            overlays::Toast::instance().show(ui::strings::kReaderJumpInvalid, 1500);
            self->request_repaint();
            return;
        }
    }

    char* end = nullptr;
    const unsigned long val = std::strtoul(text, &end, 10);
    if (end == text || val < 1UL || val > self->book_.page_count())
    {
        overlays::Toast::instance().show(ui::strings::kReaderJumpInvalid, 1500);
        self->request_repaint();
        return;
    }

    self->go_to_page(static_cast<uint16_t>(val - 1UL));
}

void ReaderApp::handle_menu_hit(MenuHit hit)
{
    switch (hit)
    {
        case MenuHit::FontPrev:     cycle_font_scale(-1); break;
        case MenuHit::FontNext:     cycle_font_scale(1);  break;
        case MenuHit::RefreshPrev: cycle_refresh_mode(-1); break;
        case MenuHit::RefreshNext: cycle_refresh_mode(1);  break;
        case MenuHit::JumpPrev10:   go_page_relative(-10); break;
        case MenuHit::PagePrev:     go_prev_page(); break;
        case MenuHit::PageNext:     go_next_page(); break;
        case MenuHit::JumpNext10:   go_page_relative(10); break;
        case MenuHit::Shelf:        return_to_shelf(); break;
        case MenuHit::JumpPage:     open_page_jump(); break;
        case MenuHit::Lock:         go_lock_screen(); break;
        default: break;
    }
}

bool ReaderApp::handle_reading_tap(int16_t x, int16_t y)
{
    const ReaderLayout ly = layout();

    if (menu_visible_)
    {
        const MenuHit hit = hit_menu(x, y, ly);
        if (hit != MenuHit::None)
        {
            handle_menu_hit(hit);
            return true;
        }
    }

    if (!in_reading_content(x, y))
        return true;

    const int16_t third   = static_cast<int16_t>(Theme::kScreenW / 3);
    const int16_t local_x = x;

    if (local_x < third)
    {
        go_prev_page();
        return true;
    }
    if (local_x > third * 2)
    {
        go_next_page();
        return true;
    }

    menu_visible_ = !menu_visible_;
    request_repaint();
    return true;
}

shell::InputResult ReaderApp::on_input(const ::app::ebook::input::Event& ev)
{
    using ::app::ebook::input::EventType;

    if (overlays::Keyboard::instance().is_open())
        return {};

    if (screen_ == Screen::Shelf)
    {
        if (ev.type == EventType::Tap &&
            ui::widgets::hit_toolbar_back(static_cast<int16_t>(ev.x), static_cast<int16_t>(ev.y)))
            return {};

        const auto out = list_.handle_input(ev);
        if (out.scroll_changed)
            request_repaint();
        if (out.consumed)
            return {true};
        return {};
    }

    if (screen_ == Screen::Opening)
        return {true};

    if (screen_ == Screen::Reading)
    {
        if (ev.type == EventType::Tap)
        {
            if (handle_reading_tap(static_cast<int16_t>(ev.x), static_cast<int16_t>(ev.y)))
                return {true};
        }
        if (ev.type == EventType::SwipeLeft)
        {
            go_next_page();
            return {true};
        }
        if (ev.type == EventType::SwipeRight)
        {
            go_prev_page();
            return {true};
        }
        return {true};
    }

    return {};
}

} // namespace app::ebook::apps::reader
