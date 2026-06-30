#include "apps/files/files_app.hpp"

#include <cstdio>
#include <cstring>
#include <mutex>

#include "apps/app_id.hpp"
#include "apps/drawing/drawing_app.hpp"
#include "apps/gallery/gallery_app.hpp"
#include "apps/music/music_app.hpp"
#include "apps/reader/reader_app.hpp"
#include "gfx/icon.hpp"
#include "gfx/text_layout.hpp"
#include "overlays/toast.hpp"
#include "router/page_id.hpp"
#include "router/router.hpp"
#include "storage/storage.hpp"
#include "text/path_encoding.hpp"
#include "ui/strings.hpp"
#include "ui/theme.hpp"
#include "ui/ui_bus.hpp"
#include "ui/widgets.hpp"

namespace app::ebook::apps::files {

namespace {

using ui::Theme;

constexpr uint16_t kFileDlgW       = 168;
constexpr uint16_t kFileDlgActionH = 92;
constexpr uint16_t kFileDlgInfoH   = 118;
constexpr uint16_t kFileDlgDeleteH = 76;
constexpr uint16_t kFileBtnH       = 28;
constexpr uint8_t  kActionBtnNum   = 3;
constexpr uint8_t  kCloseBtnSize   = 20;
constexpr uint8_t  kCloseIconSize  = 14;
constexpr uint8_t  kCloseBtnPad    = 2;
constexpr uint32_t kReloadStack    = 6144;
constexpr int64_t  kScrollRepaintMs = 120;

struct ReloadJob
{
    char              cwd[VfsBrowser::kPathCap]{};
    VfsBrowser::Entry entries[VfsBrowser::kMaxEntries]{};
    uint8_t           count{0};
    bool              truncated{false};
    bool              has_parent{false};
};

ReloadJob  g_reload_job{};
std::mutex g_reload_mtx;

} // namespace

FilesApp& FilesApp::instance()
{
    static FilesApp s;
    return s;
}

FilesApp::FilesApp()
{
    list_.set_area(Theme::list_region());
    list_.set_provider([this](uint8_t i, ui::widgets::RowStyle& rs) { fill_row(i, rs); });
    list_.set_tap_handler([this](uint8_t i) { on_row_tap(i); });
}

const char* FilesApp::title() const
{
    if (browser_.mode() == VfsBrowser::Mode::Roots)
        return ui::strings::kAppFiles;

    display_name(browser_.browse_title(), title_buf_, sizeof(title_buf_),
                 static_cast<uint16_t>(Theme::kScreenW - Theme::kPadLg * 2));
    return title_buf_;
}

uint32_t FilesApp::icon_cp() const { return gfx::icon::kFaFolderOpen; }

void FilesApp::request_scroll_repaint()
{
    if (!request_repaint_if_ready(last_scroll_repaint_ms_, kScrollRepaintMs, false))
        return;
    request_repaint();
}

void FilesApp::on_enter()
{
    cancel_browse_reload();
    browser_.show_roots();
    list_.set_scroll(0);
    sync_list_total();
    close_dialog();
    request_repaint();
}

void FilesApp::on_exit()
{
    cancel_browse_reload();
    browser_.show_roots();
    list_.set_scroll(0);
    close_dialog();
}

void FilesApp::cancel_browse_reload()
{
    reload_task_.reset();
    loading_ = false;
}

void FilesApp::close_dialog()
{
    dlg_           = FileDlg::None;
    sel_idx_       = 0;
    sel_path_[0]   = '\0';
    sel_entry_     = {};
    sel_is_system_ = false;
}

void FilesApp::sync_list_total()
{
    list_.set_total(browser_.row_count());
}

void FilesApp::rebuild_row_cache()
{
    std::memset(row_icon_cp_, 0, sizeof(row_icon_cp_));
    const uint8_t n = browser_.dir_entry_count();
    const uint16_t  label_w =
        static_cast<uint16_t>(Theme::kScreenW - 80);

    for (uint8_t i = 0; i < n; ++i)
    {
        const VfsBrowser::Entry& e = browser_.entry_at(i);
        display_name(e.name, row_label_[i], sizeof(row_label_[i]), label_w);
        row_icon_cp_[i] = row_icon(e);
        if (!e.is_dir)
            gfx::format_file_size(e.size_bytes, row_value_[i], sizeof(row_value_[i]));
        else
            row_value_[i][0] = '\0';
    }
}

void FilesApp::on_browse_ready()
{
    loading_ = false;
    reload_task_.reset();
    rebuild_row_cache();
    sync_list_total();
    request_repaint();
}

void FilesApp::finish_browse_reload()
{
    {
        std::lock_guard<std::mutex> lk(g_reload_mtx);
        browser_.adopt_scan(g_reload_job.entries, g_reload_job.count, g_reload_job.truncated,
                            g_reload_job.has_parent);
    }
    on_browse_ready();
}

void FilesApp::schedule_browse_reload()
{
    if (browser_.mode() != VfsBrowser::Mode::Browse || browser_.cwd()[0] == '\0')
        return;

    cancel_browse_reload();
    loading_ = true;
    list_.set_scroll(0);
    sync_list_total();
    request_repaint();

    {
        std::lock_guard<std::mutex> lk(g_reload_mtx);
        std::strncpy(g_reload_job.cwd, browser_.cwd(), sizeof(g_reload_job.cwd) - 1);
        g_reload_job.cwd[sizeof(g_reload_job.cwd) - 1] = '\0';
    }

    ::app::sys::task::Cfg cfg =
        ::app::sys::task::Cfg::light("files_reload", ::app::sys::task::Priority::LOW);
    cfg.stack_size = kReloadStack;
    cfg.use_psram  = true;

    reload_task_ = std::make_unique<::app::sys::task::Task>(
        [](void* /*arg*/) {
            {
                std::lock_guard<std::mutex> lk(g_reload_mtx);
                g_reload_job.count     = 0;
                g_reload_job.truncated = false;
                (void)VfsBrowser::scan_entries(g_reload_job.cwd, g_reload_job.entries,
                                                g_reload_job.count, g_reload_job.truncated);
                g_reload_job.has_parent = !VfsBrowser::is_storage_root(g_reload_job.cwd);
            }

            (void)ui::UiBus::get_instance().post_system_hint(ui::SystemHintKind::FilesReloadDone,
                                                              0);
        },
        cfg, nullptr);

    if (!reload_task_->start())
    {
        reload_task_.reset();
        browser_.reload();
        on_browse_ready();
    }
}

void FilesApp::on_ui_event(const ui::UiEvent& ev)
{
    if (ev.kind != ui::UiEventKind::SystemHint)
        return;
    if (ev.payload.system.hint != ui::SystemHintKind::FilesReloadDone)
        return;
    finish_browse_reload();
}

bool FilesApp::ends_with_ci(const char* name, const char* ext)
{
    if (name == nullptr || ext == nullptr)
        return false;
    const size_t nl = std::strlen(name);
    const size_t el = std::strlen(ext);
    if (nl < el)
        return false;
    const char* tail = name + (nl - el);
    for (size_t i = 0; i < el; ++i)
    {
        char a = tail[i];
        char b = ext[i];
        if (a >= 'A' && a <= 'Z')
            a = static_cast<char>(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z')
            b = static_cast<char>(b + ('a' - 'A'));
        if (a != b)
            return false;
    }
    return true;
}

FilesApp::OpenKind FilesApp::classify_open(const char* name)
{
    if (name == nullptr)
        return OpenKind::Unsupported;
    if (ends_with_ci(name, ".txt"))
        return OpenKind::Txt;
    if (ends_with_ci(name, ".bmp"))
        return OpenKind::Bmp;
    if (ends_with_ci(name, ".jpg") || ends_with_ci(name, ".jpeg"))
        return OpenKind::Jpeg;
    if (ends_with_ci(name, ".mp3") || ends_with_ci(name, ".wav"))
        return OpenKind::Audio;
    if (ends_with_ci(name, ".ttf") || ends_with_ci(name, ".bin"))
        return OpenKind::Unsupported;
    return OpenKind::Unsupported;
}

const char* FilesApp::type_label(OpenKind kind, const char* name)
{
    switch (kind)
    {
        case OpenKind::Txt:   return ui::strings::kFilesTypeTxt;
        case OpenKind::Bmp:   return ui::strings::kFilesTypeBmp;
        case OpenKind::Jpeg:  return ui::strings::kFilesTypePhoto;
        case OpenKind::Audio: return ui::strings::kFilesTypeAudio;
        default:
            if (ends_with_ci(name, ".ttf") || ends_with_ci(name, ".bin"))
                return ui::strings::kFilesTypeFont;
            return ui::strings::kFilesTypeFile;
    }
}

void FilesApp::display_name(const char* raw, char* out, size_t out_cap, uint16_t max_w)
{
    char norm[48];
    (void)text::normalize_path_segment(raw, norm, sizeof(norm));
    (void)gfx::truncate_text(norm, Theme::kFontBody, max_w, out, out_cap);
}

uint32_t FilesApp::row_icon(const VfsBrowser::Entry& e)
{
    if (e.is_dir)
        return gfx::icon::kFaFolder;
    if (ends_with_ci(e.name, ".txt"))
        return gfx::icon::kFaBookReader;
    if (ends_with_ci(e.name, ".bmp"))
        return gfx::icon::kFaPaintBrush;
    if (ends_with_ci(e.name, ".jpg") || ends_with_ci(e.name, ".jpeg"))
        return gfx::icon::kFaImages;
    if (ends_with_ci(e.name, ".mp3") || ends_with_ci(e.name, ".wav"))
        return gfx::icon::kFaMusic;
    return gfx::icon::kFaFile;
}

core::Rect FilesApp::file_dlg_rect(FileDlg dlg)
{
    uint16_t h = kFileDlgActionH;
    if (dlg == FileDlg::Info)
        h = kFileDlgInfoH;
    else if (dlg == FileDlg::DeleteConfirm)
        h = kFileDlgDeleteH;

    const int16_t x = static_cast<int16_t>((Theme::kScreenW - kFileDlgW) / 2);
    const int16_t y = static_cast<int16_t>((Theme::kScreenH - h) / 2);
    return core::Rect{x, y, kFileDlgW, h};
}

core::Rect FilesApp::file_dlg_close_rect(FileDlg dlg)
{
    const core::Rect dlg_r = file_dlg_rect(dlg);
    return core::Rect{
        static_cast<int16_t>(dlg_r.right() - kCloseBtnSize - kCloseBtnPad),
        static_cast<int16_t>(dlg_r.y + kCloseBtnPad),
        kCloseBtnSize, kCloseBtnSize};
}

core::Rect FilesApp::file_action_btn_rect(uint8_t idx)
{
    const core::Rect dlg = file_dlg_rect(FileDlg::Action);
    const uint16_t   w3  = static_cast<uint16_t>(dlg.w / kActionBtnNum);
    const int16_t    x   = static_cast<int16_t>(dlg.x + idx * w3);
    const int16_t    y   = static_cast<int16_t>(dlg.bottom() - kFileBtnH);
    return core::Rect{x, y, w3, kFileBtnH};
}

core::Rect FilesApp::file_confirm_btn_rect(bool yes)
{
    const core::Rect dlg  = file_dlg_rect(FileDlg::DeleteConfirm);
    const uint16_t   half = static_cast<uint16_t>(dlg.w / 2U);
    const int16_t    x    = yes ? dlg.x : static_cast<int16_t>(dlg.x + half);
    const int16_t    y    = static_cast<int16_t>(dlg.bottom() - kFileBtnH);
    return core::Rect{x, y, half, kFileBtnH};
}

core::Rect FilesApp::file_info_back_rect()
{
    const core::Rect dlg = file_dlg_rect(FileDlg::Info);
    return core::Rect{dlg.x, static_cast<int16_t>(dlg.bottom() - kFileBtnH), dlg.w, kFileBtnH};
}

void FilesApp::build_info_lines()
{
    display_name(sel_entry_.name, dlg_title_, sizeof(dlg_title_), 120);

    char sz[16];
    gfx::format_file_size(sel_entry_.size_bytes, sz, sizeof(sz));
    (void)std::snprintf(info_lines_[0], sizeof(info_lines_[0]), "%s: %s",
                        ui::strings::kFilesInfoName, dlg_title_);
    (void)std::snprintf(info_lines_[1], sizeof(info_lines_[1]), "%s: %s",
                        ui::strings::kFilesInfoSize, sz);

    char path_shown[40];
    display_name(sel_path_, path_shown, sizeof(path_shown), 100);
    (void)std::snprintf(info_lines_[2], sizeof(info_lines_[2]), "%s: %s",
                        ui::strings::kFilesInfoPath, path_shown);

    const OpenKind kind = classify_open(sel_entry_.name);
    (void)std::snprintf(info_lines_[3], sizeof(info_lines_[3]), "%s: %s",
                        ui::strings::kFilesInfoType, type_label(kind, sel_entry_.name));
}

void FilesApp::fill_row(uint8_t idx, ui::widgets::RowStyle& rs)
{
    rs = {};

    if (browser_.mode() == VfsBrowser::Mode::Roots)
    {
        const VfsBrowser::RootVol* vol = VfsBrowser::root_at_visible(idx);
        if (vol == nullptr)
            return;
        const char* label = vol->label;
        if (vol->kind == ::app::common::storage::MountKind::Internal)
            label = ui::strings::kFilesInternal;
        else if (vol->kind == ::app::common::storage::MountKind::Sd)
            label = ui::strings::kFilesSd;
        else if (vol->kind == ::app::common::storage::MountKind::Assets)
            label = ui::strings::kFilesAssets;
        rs.label        = label;
        rs.icon_cp      = gfx::icon::kFaHdd;
        rs.show_chevron = true;
        return;
    }

    if (browser_.has_parent_row() && idx == 0)
    {
        rs.label   = ui::strings::kFilesParent;
        rs.icon_cp = gfx::icon::kFaChevronLeft;
        return;
    }

    const uint8_t base = browser_.has_parent_row() ? 1U : 0U;
    if (idx < base)
        return;
    const uint8_t eidx = static_cast<uint8_t>(idx - base);
    if (eidx >= browser_.dir_entry_count())
        return;

    rs.label        = row_label_[eidx];
    rs.icon_cp      = row_icon_cp_[eidx];
    rs.show_chevron = browser_.entry_at(eidx).is_dir;
    if (row_value_[eidx][0] != '\0')
        rs.value = row_value_[eidx];
}

void FilesApp::show_file_action(uint8_t entry_idx)
{
    if (loading_)
        return;
    if (!browser_.entry_path(entry_idx, sel_path_, sizeof(sel_path_)))
        return;

    sel_idx_       = entry_idx;
    sel_entry_     = browser_.entry_at(entry_idx);
    sel_is_system_ = ::app::common::storage::path_is_system(sel_path_);
    display_name(sel_entry_.name, dlg_title_, sizeof(dlg_title_), 120);

    if (sel_is_system_)
    {
        build_info_lines();
        dlg_ = FileDlg::Info;
    }
    else
    {
        dlg_ = FileDlg::Action;
    }
    request_repaint();
}

void FilesApp::on_row_tap(uint8_t idx)
{
    if (browser_.mode() == VfsBrowser::Mode::Roots)
    {
        const VfsBrowser::RootVol* vol = VfsBrowser::root_at_visible(idx);
        if (vol == nullptr)
            return;
        if (!browser_.open_root(vol->path, vol->label))
        {
            overlays::Toast::instance().show(ui::strings::kFilesNotMounted, 2000);
            request_repaint();
            return;
        }
        schedule_browse_reload();
        return;
    }

    if (browser_.has_parent_row() && idx == 0)
    {
        cancel_browse_reload();
        browser_.go_up();
        list_.set_scroll(0);
        if (browser_.mode() == VfsBrowser::Mode::Roots)
        {
            sync_list_total();
            request_repaint();
        }
        else
            schedule_browse_reload();
        return;
    }

    if (loading_)
        return;

    const uint8_t base = browser_.has_parent_row() ? 1U : 0U;
    if (idx < base)
        return;
    const uint8_t eidx = static_cast<uint8_t>(idx - base);
    const VfsBrowser::Entry& e = browser_.entry_at(eidx);
    if (e.name[0] == '\0')
        return;

    if (e.is_dir)
    {
        if (browser_.enter_dir(eidx))
            schedule_browse_reload();
        return;
    }

    show_file_action(eidx);
}

void FilesApp::open_selected_file()
{
    switch (classify_open(sel_entry_.name))
    {
        case OpenKind::Txt:
            reader::ReaderApp::request_open_on_enter(sel_path_);
            (void)router::Router::instance().navigate(router::page_app(AppId::Reader));
            close_dialog();
            return;
        case OpenKind::Bmp:
            drawing::DrawingApp::request_open_on_enter(sel_path_);
            (void)router::Router::instance().navigate(router::page_app(AppId::Drawing));
            close_dialog();
            return;
        case OpenKind::Jpeg:
            gallery::GalleryApp::request_open_on_enter(sel_path_);
            (void)router::Router::instance().navigate(router::page_app(AppId::Gallery));
            close_dialog();
            return;
        case OpenKind::Audio:
            music::MusicApp::request_play_on_enter(sel_path_);
            (void)router::Router::instance().navigate(router::page_app(AppId::Music));
            close_dialog();
            return;
        default:
            overlays::Toast::instance().show(ui::strings::kFilesOpenUnsupported, 2000);
            request_repaint();
            return;
    }
}

void FilesApp::delete_selected_file()
{
    if (sel_is_system_)
    {
        overlays::Toast::instance().show(ui::strings::kFilesSystemProtected, 2000);
        dlg_ = FileDlg::Action;
        request_repaint();
        return;
    }

    if (!browser_.delete_file(sel_idx_))
    {
        overlays::Toast::instance().show(ui::strings::kFilesDeleteFail, 2000);
        dlg_ = FileDlg::Action;
        request_repaint();
        return;
    }

    close_dialog();
    schedule_browse_reload();
}

shell::InputResult FilesApp::handle_dialog_tap(int16_t x, int16_t y)
{
    if (dlg_ != FileDlg::None && file_dlg_close_rect(dlg_).contains(x, y))
    {
        close_dialog();
        request_repaint();
        return {true};
    }

    switch (dlg_)
    {
        case FileDlg::Action:
            if (file_action_btn_rect(0).contains(x, y))
            {
                build_info_lines();
                dlg_ = FileDlg::Info;
                request_repaint();
                return {true};
            }
            if (file_action_btn_rect(1).contains(x, y))
            {
                open_selected_file();
                return {true};
            }
            if (file_action_btn_rect(2).contains(x, y))
            {
                dlg_ = FileDlg::DeleteConfirm;
                request_repaint();
                return {true};
            }
            return {true};

        case FileDlg::Info:
            if (file_info_back_rect().contains(x, y))
            {
                if (sel_is_system_)
                    close_dialog();
                else
                    dlg_ = FileDlg::Action;
                request_repaint();
                return {true};
            }
            return {true};

        case FileDlg::DeleteConfirm:
            if (file_confirm_btn_rect(true).contains(x, y))
            {
                delete_selected_file();
                return {true};
            }
            if (file_confirm_btn_rect(false).contains(x, y))
            {
                dlg_ = FileDlg::Action;
                request_repaint();
                return {true};
            }
            return {true};

        default:
            return {};
    }
}

bool FilesApp::handle_toolbar_back()
{
    if (dlg_ != FileDlg::None)
    {
        if (dlg_ == FileDlg::Info && !sel_is_system_)
            dlg_ = FileDlg::Action;
        else
            close_dialog();
        request_repaint();
        return true;
    }

    if (browser_.mode() == VfsBrowser::Mode::Roots)
        return false;

    cancel_browse_reload();
    browser_.go_up();
    list_.set_scroll(0);
    if (browser_.mode() == VfsBrowser::Mode::Roots)
    {
        sync_list_total();
        request_repaint();
    }
    else
        schedule_browse_reload();
    return true;
}

void FilesApp::paint_dlg_close_btn(gfx::Canvas& c, FileDlg dlg)
{
    const core::Rect btn = file_dlg_close_rect(dlg);
    c.rect(btn, 1);

    const int16_t ix = static_cast<int16_t>(btn.x + (btn.w - kCloseIconSize) / 2);
    const int16_t iy = static_cast<int16_t>(btn.y + (btn.h - kCloseIconSize) / 2);
    c.glyph(ix, iy, gfx::icon::kFaTimes, kCloseIconSize, gfx::FontFace::Icon);
}

void FilesApp::paint_action_dialog(gfx::Canvas& c)
{
    const core::Rect dlg = file_dlg_rect(FileDlg::Action);
    c.fill(dlg, gfx::Ink::White);
    c.rect(dlg, 2);

    gfx::Canvas::TextStyle ts{};
    ts.size_px = Theme::kFontBody;
    ts.h       = gfx::HAlign::Center;
    ts.v       = gfx::VAlign::Middle;
    ts.padding = 2;

    const uint16_t title_w =
        static_cast<uint16_t>(dlg.w - kCloseBtnSize - kCloseBtnPad * 2);
    const core::Rect title{dlg.x, static_cast<int16_t>(dlg.y + 4), title_w,
                           static_cast<uint16_t>(dlg.h - kFileBtnH - 8)};
    c.text_in(title, dlg_title_, ts);

    static constexpr const char* kLabels[kActionBtnNum] = {
        ui::strings::kFilesView,
        ui::strings::kFilesOpen,
        ui::strings::kFilesDelete,
    };
    for (uint8_t i = 0; i < kActionBtnNum; ++i)
    {
        const core::Rect btn = file_action_btn_rect(i);
        c.rect(btn, 1);
        c.text_in(btn, kLabels[i], ts);
        if (i > 0)
            c.vline(static_cast<int16_t>(btn.x), static_cast<int16_t>(btn.y), kFileBtnH);
    }
}

void FilesApp::paint_info_dialog(gfx::Canvas& c)
{
    const core::Rect dlg = file_dlg_rect(FileDlg::Info);
    c.fill(dlg, gfx::Ink::White);
    c.rect(dlg, 2);

    gfx::Canvas::TextStyle ts{};
    ts.size_px = Theme::kFontSmall;
    ts.h       = gfx::HAlign::Left;
    ts.v       = gfx::VAlign::Top;
    ts.padding = 2;

    const uint16_t line_w =
        static_cast<uint16_t>(dlg.w - kCloseBtnSize - kCloseBtnPad * 2 - 4);
    int16_t y = static_cast<int16_t>(dlg.y + 6);
    for (uint8_t i = 0; i < 4; ++i)
    {
        const core::Rect line{static_cast<int16_t>(dlg.x + 4), y, line_w, 14};
        c.text_in(line, info_lines_[i], ts);
        y = static_cast<int16_t>(y + 14);
    }

    const core::Rect back = file_info_back_rect();
    c.rect(back, 1);
    ts.h       = gfx::HAlign::Center;
    ts.v       = gfx::VAlign::Middle;
    ts.size_px = Theme::kFontBody;
    c.text_in(back, ui::strings::kBack, ts);
}

void FilesApp::paint_delete_dialog(gfx::Canvas& c)
{
    const core::Rect dlg = file_dlg_rect(FileDlg::DeleteConfirm);
    c.fill(dlg, gfx::Ink::White);
    c.rect(dlg, 2);

    gfx::Canvas::TextStyle ts{};
    ts.size_px = Theme::kFontBody;
    ts.h       = gfx::HAlign::Center;
    ts.v       = gfx::VAlign::Middle;
    ts.padding = 2;

    const uint16_t msg_w =
        static_cast<uint16_t>(dlg.w - kCloseBtnSize - kCloseBtnPad * 2);
    const core::Rect msg{dlg.x, static_cast<int16_t>(dlg.y + 4), msg_w,
                         static_cast<uint16_t>(dlg.h - kFileBtnH - 6)};
    c.text_in(msg, ui::strings::kFilesDeleteAsk, ts);

    const core::Rect yes_btn = file_confirm_btn_rect(true);
    const core::Rect no_btn  = file_confirm_btn_rect(false);
    c.rect(yes_btn, 1);
    c.rect(no_btn, 1);
    c.text_in(yes_btn, ui::strings::kDrawYes, ts);
    c.text_in(no_btn, ui::strings::kDrawNo, ts);
    c.vline(static_cast<int16_t>(dlg.x + dlg.w / 2),
            static_cast<int16_t>(yes_btn.y), kFileBtnH);
}

void FilesApp::paint_file_dialog(gfx::Canvas& c)
{
    switch (dlg_)
    {
        case FileDlg::Action:
            paint_action_dialog(c);
            break;
        case FileDlg::Info:
            paint_info_dialog(c);
            break;
        case FileDlg::DeleteConfirm:
            paint_delete_dialog(c);
            break;
        default:
            break;
    }

    if (dlg_ != FileDlg::None)
        paint_dlg_close_btn(c, dlg_);
}

void FilesApp::paint(gfx::Canvas& c)
{
    ui::widgets::toolbar(c, title());

    if (loading_ && browser_.mode() == VfsBrowser::Mode::Browse)
    {
        list_.paint(c);
        gfx::Canvas::TextStyle ts{};
        ts.size_px = Theme::kFontBody;
        ts.h       = gfx::HAlign::Center;
        ts.v       = gfx::VAlign::Middle;
        c.text_in(Theme::list_region(), ui::strings::kFilesLoading, ts);
        return;
    }

    list_.paint(c);

    if (browser_.mode() == VfsBrowser::Mode::Roots && list_.total() == 0)
    {
        gfx::Canvas::TextStyle ts{};
        ts.size_px = Theme::kFontBody;
        ts.h       = gfx::HAlign::Center;
        ts.v       = gfx::VAlign::Middle;
        c.text_in(Theme::list_region(), ui::strings::kFilesEmpty, ts);
        return;
    }

    if (browser_.truncated())
    {
        const core::Rect hint{
            Theme::kPadLg,
            static_cast<int16_t>(Theme::kListStartY + Theme::kListRegionH - Theme::kFontSmall - 2),
            static_cast<uint16_t>(Theme::kScreenW - Theme::kPadLg * 2),
            static_cast<uint16_t>(Theme::kFontSmall + 2)};
        gfx::Canvas::TextStyle ts{};
        ts.size_px = Theme::kFontSmall;
        ts.h       = gfx::HAlign::Center;
        ts.v       = gfx::VAlign::Bottom;
        c.text_in(hint, ui::strings::kFilesTruncated, ts);
    }
}

void FilesApp::paint_overlay(gfx::Canvas& c)
{
    if (dlg_ != FileDlg::None)
        paint_file_dialog(c);
}

shell::InputResult FilesApp::on_input(const ::app::ebook::input::Event& ev)
{
    using ::app::ebook::input::EventType;

    if (dlg_ != FileDlg::None)
    {
        if (ev.type == EventType::Tap)
            return handle_dialog_tap(static_cast<int16_t>(ev.x), static_cast<int16_t>(ev.y));
        return {true};
    }

    if (ev.type == EventType::Tap &&
        ui::widgets::hit_toolbar_back(static_cast<int16_t>(ev.x), static_cast<int16_t>(ev.y)))
    {
        if (handle_toolbar_back())
            return {true};
        return {};
    }

    const auto out = list_.handle_input(ev);
    if (out.scroll_changed)
        request_scroll_repaint();
    if (out.consumed)
        return {true};
    return {};
}

} // namespace app::ebook::apps::files
