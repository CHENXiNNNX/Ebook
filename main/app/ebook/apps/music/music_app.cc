#include "apps/music/music_app.hpp"

#include <cstdio>
#include <cstring>

#include "apps/music/music_library.hpp"
#include "apps/music/music_store.hpp"
#include "overlays/toast.hpp"
#include "gfx/icon.hpp"
#include "gfx/text_layout.hpp"
#include "gfx/text_layout.hpp"
#include "platform/music_player.hpp"
#include "ui/strings.hpp"
#include "ui/theme.hpp"
#include "system/task/task.hpp"
#include "ui/ui_bus.hpp"
#include "ui/widgets.hpp"

namespace app::ebook::apps::music {

namespace {

using ui::Theme;
using platform::MusicPlayer;
using platform::MusicPlayState;

constexpr uint32_t kScanStack    = 3072;
constexpr uint16_t kCtrlBarH     = 44;
constexpr uint8_t  kCtrlIconSize = 22;

void fmt_mmss(uint16_t sec, char* out, size_t cap)
{
    if (out == nullptr || cap < 6) return;
    const uint16_t m = static_cast<uint16_t>(sec / 60U);
    const uint16_t s = static_cast<uint16_t>(sec % 60U);
    (void)std::snprintf(out, cap, "%u:%02u",
                        static_cast<unsigned>(m), static_cast<unsigned>(s));
}

void title_from_path(const char* path, char* out, size_t cap)
{
    if (path == nullptr || out == nullptr || cap == 0)
        return;
    const char* name = std::strrchr(path, '/');
    name             = (name != nullptr) ? (name + 1) : path;
    (void)std::strncpy(out, name, cap - 1);
    out[cap - 1] = '\0';
    char* dot    = std::strrchr(out, '.');
    if (dot != nullptr)
        *dot = '\0';
}

void sync_track_index(uint8_t& track_idx)
{
    auto& player = MusicPlayer::get_instance();
    const char*  cur = player.current_path();
    if (cur == nullptr || cur[0] == '\0')
        return;

    auto& lib = MusicLibrary::get_instance();
    int8_t  idx = lib.find_index(cur);
    if (idx < 0)
        (void)lib.add_track_from_path(cur);
    idx = lib.find_index(cur);
    if (idx >= 0)
        track_idx = static_cast<uint8_t>(idx);
}

void fmt_size_or_dur(uint32_t bytes, uint16_t dur_sec, char* out, size_t cap)
{
    if (out == nullptr || cap < 4) return;
    if (dur_sec > 0)
    {
        char t[16];
        fmt_mmss(dur_sec, t, sizeof(t));
        (void)std::snprintf(out, cap, "%s", t);
        return;
    }
    gfx::format_file_size(bytes, out, cap);
}

constexpr uint8_t kPlayPathCap = 96;

bool s_play_on_enter_ = false;
char s_play_path_[kPlayPathCap]{};

} // namespace

MusicApp& MusicApp::instance()
{
    static MusicApp s;
    return s;
}

MusicApp::MusicApp()
{
    list_.set_area(Theme::list_region());
    list_.set_provider([](uint8_t i, ui::widgets::RowStyle& rs) {
        const auto& lib = MusicLibrary::get_instance();
        if (i >= lib.count()) return;
        const auto& t = lib.at(i);
        static char sub[24];
        fmt_size_or_dur(t.size_bytes, t.duration_sec, sub, sizeof(sub));
        rs.label        = t.title;
        rs.value        = sub;
        rs.icon_cp      = gfx::icon::kFaMusic;
        rs.show_chevron = true;
    });
    list_.set_tap_handler([this](uint8_t i) { play_index(i); });
}

const char* MusicApp::title() const
{
    return (view_ == View::Player) ? ui::strings::kMusicPlaying : ui::strings::kAppMusic;
}

uint32_t MusicApp::icon_cp() const { return gfx::icon::kFaMusic; }

core::Rect MusicApp::player_body_rect()
{
    return core::Rect{
        0, static_cast<int16_t>(Theme::kListStartY), Theme::kScreenW,
        static_cast<uint16_t>(Theme::kScreenH - Theme::kListStartY - kCtrlBarH)};
}

core::Rect MusicApp::player_controls_rect()
{
    return core::Rect{0, static_cast<int16_t>(Theme::kScreenH - kCtrlBarH),
                      Theme::kScreenW, kCtrlBarH};
}

core::Rect MusicApp::ctrl_btn_rect(uint8_t idx)
{
    const core::Rect bar = player_controls_rect();
    const uint16_t   w3  = static_cast<uint16_t>(bar.w / 3);
    return core::Rect{static_cast<int16_t>(bar.x + idx * w3), bar.y, w3, bar.h};
}

void MusicApp::set_view(View v)
{
    view_ = v;
    request_repaint();
}

void MusicApp::request_play_on_enter(const char* path)
{
    if (path == nullptr || path[0] == '\0')
        return;
    (void)std::strncpy(s_play_path_, path, kPlayPathCap - 1);
    s_play_path_[kPlayPathCap - 1] = '\0';
    s_play_on_enter_ = true;
}

void MusicApp::on_enter()
{
    view_     = View::Library;
    scanning_ = false;
    list_.set_scroll(0);

    if (s_play_on_enter_)
    {
        s_play_on_enter_ = false;
        auto& lib = MusicLibrary::get_instance();
        (void)lib.add_track_from_path(s_play_path_);

        const int8_t idx = lib.find_index(s_play_path_);
        if (idx >= 0)
            track_idx_ = static_cast<uint8_t>(idx);

        if (MusicPlayer::get_instance().play(s_play_path_))
            view_ = View::Player;
        else
            overlays::Toast::instance().show(ui::strings::kMusicPlayFail, 2000);

        start_scan();
        request_repaint();
        return;
    }

    start_scan();
}

void MusicApp::on_exit()
{
    MusicPlayer::get_instance().stop();
    scan_task_.reset();
    scanning_ = false;
    view_     = View::Library;
}

void MusicApp::start_scan()
{
    if (scanning_) return;
    scanning_ = true;
    request_repaint();

    scan_task_.reset();

    ::app::sys::task::Cfg cfg = ::app::sys::task::Cfg::light("music_scan",
                                                             ::app::sys::task::Priority::LOW);
    cfg.stack_size = kScanStack;
    cfg.use_psram  = true;

    scan_task_ = std::make_unique<::app::sys::task::Task>(
        [](void* /*arg*/) {
            (void)MusicLibrary::get_instance().scan();
            (void)ui::UiBus::get_instance().post_system_hint(
                ui::SystemHintKind::MusicScanDone, 0);
        },
        cfg, nullptr);

    if (!scan_task_->start())
    {
        scan_task_.reset();
        scanning_ = false;
        request_repaint();
    }
}

void MusicApp::on_ui_event(const ui::UiEvent& ev)
{
    if (ev.kind != ui::UiEventKind::SystemHint) return;

    switch (ev.payload.system.hint)
    {
        case ui::SystemHintKind::MusicScanDone:
        {
            scanning_ = false;
            scan_task_.reset();
            const auto& lib = MusicLibrary::get_instance();
            list_.set_total(lib.count());
            list_.set_scroll(0);

            if (view_ == View::Player)
            {
                sync_track_index(track_idx_);
            }
            else
            {
                uint8_t idx = MusicStore::get_instance().last_index();
                if (idx >= lib.count())
                    idx = 0;
                track_idx_ = idx;
            }
            request_repaint();
            break;
        }
        case ui::SystemHintKind::MusicProgress:
        case ui::SystemHintKind::MusicStateChanged:
            if (view_ == View::Player)
                request_repaint();
            break;
        default:
            break;
    }
}

void MusicApp::play_index(uint8_t idx)
{
    const auto& lib = MusicLibrary::get_instance();
    if (idx >= lib.count()) return;

    track_idx_ = idx;
    MusicStore::get_instance().set_last_index(idx);

    const auto& t = lib.at(idx);
    set_view(View::Player);
    (void)MusicPlayer::get_instance().play(t.path);
}

void MusicApp::play_adjacent(int8_t delta)
{
    const auto& lib = MusicLibrary::get_instance();
    const uint8_t n = lib.count();
    if (n == 0) return;

    int16_t next = static_cast<int16_t>(track_idx_) + delta;
    if (next < 0) next = static_cast<int16_t>(n - 1);
    if (next >= static_cast<int16_t>(n)) next = 0;
    play_index(static_cast<uint8_t>(next));
}

void MusicApp::paint_library(gfx::Canvas& c)
{
    if (scanning_)
    {
        gfx::Canvas::TextStyle ts{};
        ts.size_px = Theme::kFontBody;
        ts.h       = gfx::HAlign::Center;
        ts.v       = gfx::VAlign::Middle;
        c.text_in(Theme::list_region(), ui::strings::kMusicScanning, ts);
        return;
    }
    if (MusicLibrary::get_instance().count() == 0)
    {
        static const char* const kHints[] = {
            MusicLibrary::kScanFmtHint,
            MusicLibrary::kScanPathInt,
            MusicLibrary::kScanPathSd,
        };
        ui::widgets::empty_state(c, Theme::list_region(), ui::strings::kMusicEmpty, kHints, 3);
        return;
    }
    list_.paint(c);
}

void MusicApp::paint_player(gfx::Canvas& c)
{
    const auto& lib = MusicLibrary::get_instance();

    const char* title_src = nullptr;
    char        fallback_title[48]{};
    uint16_t    total = 0;

    if (track_idx_ < lib.count())
    {
        const auto& t = lib.at(track_idx_);
        title_src     = t.title;
        total         = t.duration_sec;
    }
    else
    {
        const char* path = MusicPlayer::get_instance().current_path();
        if (path == nullptr || path[0] == '\0')
            return;
        title_from_path(path, fallback_title, sizeof(fallback_title));
        title_src = fallback_title;
        total     = MusicPlayer::get_instance().duration_sec();
    }

    const core::Rect body = player_body_rect();

    gfx::Canvas::TextStyle ts{};
    ts.size_px = Theme::kFontTitle;
    ts.h       = gfx::HAlign::Center;
    ts.v       = gfx::VAlign::Top;

    char title_show[48];
    (void)gfx::truncate_text(title_src, Theme::kFontTitle,
                             static_cast<uint16_t>(body.w > 8 ? body.w - 8 : body.w),
                             title_show, sizeof(title_show));
    const core::Rect title_box{body.x, static_cast<int16_t>(body.y + 12), body.w, 24};
    c.text_in(title_box, title_show, ts);

    auto&          player  = MusicPlayer::get_instance();
    const uint16_t elapsed = player.elapsed_sec();

    char time_line[32];
    char a[12], b[12];
    fmt_mmss(elapsed, a, sizeof(a));
    if (total > 0)
    {
        fmt_mmss(total, b, sizeof(b));
        (void)std::snprintf(time_line, sizeof(time_line), "%s / %s", a, b);
    }
    else
        (void)std::snprintf(time_line, sizeof(time_line), "%s / --", a);

    ts.size_px = Theme::kFontBody;
    const core::Rect time_box{
        body.x, static_cast<int16_t>(title_box.bottom() + 16), body.w, 18};
    c.text_in(time_box, time_line, ts);

    if (player.state() == MusicPlayState::Loading ||
        player.state() == MusicPlayState::Error)
    {
        const char* msg = ui::strings::kMusicLoading;
        if (player.state() == MusicPlayState::Error)
        {
            switch (player.last_error())
            {
                case 2: msg = ui::strings::kMusicOpenFail; break;
                case 3: msg = ui::strings::kMusicNoMem; break;
                case 5: msg = ui::strings::kMusicPlayFail; break;
                case 6: msg = ui::strings::kMusicPlayFail; break;
                default: msg = ui::strings::kMusicPlayFail; break;
            }
        }
        const core::Rect st_box{
            body.x, static_cast<int16_t>(time_box.bottom() + 8), body.w, 18};
        c.text_in(st_box, msg, ts);
    }

    const core::Rect bar = player_controls_rect();
    c.hline(bar.x, bar.y, bar.w);

    const bool playing = (player.state() == MusicPlayState::Playing);
    const uint32_t play_icon = playing ? gfx::icon::kFaPause : gfx::icon::kFaPlay;
    for (uint8_t i = 0; i < 3; ++i)
    {
        const core::Rect btn = ctrl_btn_rect(i);
        c.rect(btn, 1);
        uint32_t icon = gfx::icon::kFaStepBackward;
        if (i == 1) icon = play_icon;
        if (i == 2) icon = gfx::icon::kFaStepForward;
        const int16_t ax = static_cast<int16_t>(btn.x + (btn.w - kCtrlIconSize) / 2);
        const int16_t ay = static_cast<int16_t>(btn.y + (btn.h - kCtrlIconSize) / 2);
        c.glyph(ax, ay, icon, kCtrlIconSize, gfx::FontFace::Icon);
    }
}

void MusicApp::paint(gfx::Canvas& c)
{
    ui::widgets::toolbar(c, title());
    if (view_ == View::Library) paint_library(c);
    else                        paint_player(c);
}

shell::InputResult MusicApp::handle_player(int16_t x, int16_t y)
{
    if (!player_controls_rect().contains(x, y)) return {};

    if (ctrl_btn_rect(0).contains(x, y)) { play_adjacent(-1); return {true}; }
    if (ctrl_btn_rect(2).contains(x, y)) { play_adjacent(1); return {true}; }
    if (ctrl_btn_rect(1).contains(x, y))
    {
        auto& pl = MusicPlayer::get_instance();
        if (pl.state() == MusicPlayState::Playing) pl.pause();
        else if (pl.state() == MusicPlayState::Paused) pl.resume();
        else if (track_idx_ < MusicLibrary::get_instance().count())
            (void)pl.play(MusicLibrary::get_instance().at(track_idx_).path);
        else if (pl.current_path()[0] != '\0')
            (void)pl.play(pl.current_path());
        request_repaint();
        return {true};
    }
    return {};
}

shell::InputResult MusicApp::on_input(const ::app::ebook::input::Event& ev)
{
    using ::app::ebook::input::EventType;

    if (ev.type == EventType::Tap)
    {
        const int16_t x = static_cast<int16_t>(ev.x);
        const int16_t y = static_cast<int16_t>(ev.y);

        if (ui::widgets::hit_toolbar_back(x, y))
        {
            if (view_ == View::Player)
            {
                MusicPlayer::get_instance().stop();
                set_view(View::Library);
                return {true};
            }
            return {};
        }

        if (view_ == View::Library)
        {
            auto out = list_.handle_input(ev);
            if (out.scroll_changed) request_repaint();
            if (out.consumed) return {true};
            return {};
        }
        return handle_player(x, y);
    }

    if (view_ == View::Library)
    {
        auto out = list_.handle_input(ev);
        if (out.scroll_changed) request_repaint();
        if (out.consumed) return {true};
    }
    return {};
}

} // namespace app::ebook::apps::music
