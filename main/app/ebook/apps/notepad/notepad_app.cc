#include "apps/notepad/notepad_app.hpp"

#include <cstdio>
#include <cstring>
#include <sys/stat.h>

#include <dirent.h>

#include "gfx/icon.hpp"
#include "gfx/text_layout.hpp"
#include "overlays/keyboard.hpp"
#include "overlays/toast.hpp"
#include "ui/strings.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"

namespace app::ebook::apps::notepad {

namespace {

using ui::Theme;

constexpr const char* kNotesRoot = "/int/Ebook";
constexpr const char* kNotesDir  = "/int/Ebook/notes";

bool is_txt_name(const char* name)
{
    if (name == nullptr || name[0] == '.')
        return false;
    const size_t len = std::strlen(name);
    return (len > 4U && std::strcmp(name + len - 4U, ".txt") == 0);
}

} // namespace

NotepadApp& NotepadApp::instance()
{
    static NotepadApp s;
    return s;
}

NotepadApp::NotepadApp()
{
    list_.set_area(Theme::list_region());
    list_.set_provider([this](uint8_t i, ui::widgets::RowStyle& rs) { fill_row(i, rs); });
    list_.set_tap_handler([this](uint8_t i) { on_row_tap(i); });
}

const char* NotepadApp::title() const
{
    if (view_ == View::Edit && title_buf_[0] != '\0')
        return title_buf_;
    return ui::strings::kAppNotepad;
}

uint32_t NotepadApp::icon_cp() const { return gfx::icon::kFaStickyNote; }

void NotepadApp::on_enter()
{
    view_         = View::List;
    kb_mode_      = KbMode::None;
    clear_dialog_ = false;
    dirty_        = false;
    scroll_line_  = 0;
    path_[0]      = '\0';
    title_buf_[0] = '\0';
    body_[0]      = '\0';
    list_.set_scroll(0);
    scan_notes();
    request_repaint();
}

void NotepadApp::on_exit()
{
    if (overlays::Keyboard::instance().is_open())
        overlays::Keyboard::instance().close();
    clear_dialog_ = false;
    if (view_ == View::Edit && dirty_)
        save_note();
    view_    = View::List;
    kb_mode_ = KbMode::None;
}

bool NotepadApp::ensure_notes_dir()
{
    (void)::mkdir(kNotesRoot, 0775);
    (void)::mkdir(kNotesDir, 0775);
    struct stat st{};
    return (::stat(kNotesDir, &st) == 0);
}

void NotepadApp::title_from_path(const char* path, char* out, size_t out_size)
{
    if (path == nullptr || out == nullptr || out_size == 0)
        return;
    const char* name = std::strrchr(path, '/');
    name             = (name != nullptr) ? (name + 1) : path;
    (void)std::strncpy(out, name, out_size - 1);
    out[out_size - 1] = '\0';
    char* dot         = std::strrchr(out, '.');
    if (dot != nullptr)
        *dot = '\0';
}

void NotepadApp::sanitize_filename(const char* in, char* out, size_t out_size)
{
    if (out == nullptr || out_size == 0)
        return;
    if (in == nullptr || in[0] == '\0')
    {
        (void)std::strncpy(out, "note", out_size - 1);
        out[out_size - 1] = '\0';
        return;
    }

    size_t w = 0;
    for (size_t i = 0; in[i] != '\0' && w + 1 < out_size; ++i)
    {
        const unsigned char c = static_cast<unsigned char>(in[i]);
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<' || c == '>' || c == '|')
            continue;
        out[w++] = static_cast<char>(c);
    }
    out[w] = '\0';
    while (w > 0 && out[w - 1] == ' ')
    {
        out[w - 1] = '\0';
        --w;
    }
    if (w == 0)
    {
        (void)std::strncpy(out, "note", out_size - 1);
        out[out_size - 1] = '\0';
    }
}

void NotepadApp::scan_notes()
{
    count_ = 0;
    if (!ensure_notes_dir())
        return;

    DIR* dir = ::opendir(kNotesDir);
    if (dir == nullptr)
        return;

    struct dirent* ent = nullptr;
    while ((ent = ::readdir(dir)) != nullptr && count_ < kMaxNotes)
    {
        if (!is_txt_name(ent->d_name))
            continue;

        NoteItem& item = notes_[count_];
        const int plen = std::snprintf(item.path, sizeof(item.path), "%s/%s",
                                       kNotesDir, ent->d_name);
        if (plen < 0 || static_cast<size_t>(plen) >= sizeof(item.path))
            continue;
        title_from_path(item.path, item.title, sizeof(item.title));

        struct stat st{};
        if (::stat(item.path, &st) == 0)
            item.size = static_cast<uint32_t>(st.st_size);
        else
            item.size = 0;

        ++count_;
    }
    ::closedir(dir);

    for (uint8_t i = 0; i + 1 < count_; ++i)
    {
        for (uint8_t j = 0; j + 1 < count_ - i; ++j)
        {
            if (std::strcmp(notes_[j].title, notes_[j + 1].title) > 0)
            {
                NoteItem tmp = notes_[j];
                notes_[j]    = notes_[j + 1];
                notes_[j + 1] = tmp;
            }
        }
    }
}

bool NotepadApp::alloc_new_path(char* out, size_t cap)
{
    if (out == nullptr || cap == 0)
        return false;
    for (uint16_t i = 1; i <= 999; ++i)
    {
        (void)std::snprintf(out, cap, "%s/note_%03u.txt", kNotesDir, static_cast<unsigned>(i));
        struct stat st{};
        if (::stat(out, &st) != 0)
            return true;
    }
    return false;
}

bool NotepadApp::load_note(const char* path)
{
    if (path == nullptr)
        return false;

    FILE* f = std::fopen(path, "rb");
    if (f == nullptr)
        return false;

    const size_t n = std::fread(body_, 1, kBodyCap - 1, f);
    std::fclose(f);
    body_[n] = '\0';
    return true;
}

void NotepadApp::save_note()
{
    if (path_[0] == '\0')
        return;

    if (!ensure_notes_dir())
    {
        overlays::Toast::instance().show(ui::strings::kNoteSaveFail, 1500);
        return;
    }

    FILE* f = std::fopen(path_, "wb");
    if (f == nullptr)
    {
        overlays::Toast::instance().show(ui::strings::kNoteSaveFail, 1500);
        return;
    }

    const size_t len = std::strlen(body_);
    const size_t w   = std::fwrite(body_, 1, len, f);
    std::fclose(f);

    if (w != len)
    {
        overlays::Toast::instance().show(ui::strings::kNoteSaveFail, 1500);
        return;
    }

    dirty_ = false;
}

void NotepadApp::append_text(const char* text)
{
    if (text == nullptr || text[0] == '\0')
        return;

    size_t cur = std::strlen(body_);
    if (cur > 0 && body_[cur - 1] != '\n')
    {
        if (cur + 1 >= kBodyCap)
            return;
        body_[cur++] = '\n';
        body_[cur]   = '\0';
    }

    const size_t add = std::strlen(text);
    if (cur + add >= kBodyCap)
    {
        overlays::Toast::instance().show(ui::strings::kNoteTooLong, 1500);
        return;
    }

    (void)std::strncpy(body_ + cur, text, kBodyCap - cur - 1);
    body_[kBodyCap - 1] = '\0';
    dirty_              = true;
}

void NotepadApp::delete_last_char()
{
    const size_t len = std::strlen(body_);
    if (len == 0)
    {
        overlays::Toast::instance().show(ui::strings::kNoteNothingDel, 1200);
        return;
    }

    size_t pos = len - 1;
    while (pos > 0 && (static_cast<uint8_t>(body_[pos]) & 0xC0U) == 0x80U)
        --pos;

    body_[pos] = '\0';
    dirty_     = true;
    request_repaint();
}

void NotepadApp::clear_body()
{
    body_[0] = '\0';
    dirty_   = true;
    scroll_line_ = 0;
    request_repaint();
}

void NotepadApp::open_note(uint8_t idx)
{
    if (idx >= count_)
        return;

    sel_          = idx;
    view_         = View::Edit;
    scroll_line_  = 0;
    dirty_        = false;
    body_[0]      = '\0';
    (void)std::strncpy(path_, notes_[idx].path, sizeof(path_) - 1);
    path_[sizeof(path_) - 1] = '\0';
    title_from_path(path_, title_buf_, sizeof(title_buf_));
    (void)load_note(path_);
    request_repaint();
}

void NotepadApp::start_new_note()
{
    kb_mode_ = KbMode::NewTitle;
    overlays::KeyboardConfig kc{};
    kc.max_len = 31;
    kc.title   = ui::strings::kNoteTitleHint;
    overlays::Keyboard::instance().open(kc, on_title_kb_done, this);
}

void NotepadApp::on_title_kb_done(const char* text, void* user)
{
    auto* self = static_cast<NotepadApp*>(user);
    if (self == nullptr)
        return;
    self->kb_mode_ = KbMode::None;

    if (!self->ensure_notes_dir())
    {
        overlays::Toast::instance().show(ui::strings::kNoteSaveFail, 1500);
        self->request_repaint();
        return;
    }

    char stem[36]{};
    self->sanitize_filename(text, stem, sizeof(stem));

    char path[128]{};
    bool path_ok = false;
    for (uint8_t try_no = 0; try_no < 5; ++try_no)
    {
        if (try_no == 0)
            (void)std::snprintf(path, sizeof(path), "%s/%s.txt", kNotesDir, stem);
        else
            (void)std::snprintf(path, sizeof(path), "%s/%s_%u.txt", kNotesDir, stem,
                                static_cast<unsigned>(try_no));

        struct stat st{};
        if (::stat(path, &st) != 0)
        {
            path_ok = true;
            break;
        }
    }

    if (!path_ok && !self->alloc_new_path(path, sizeof(path)))
    {
        overlays::Toast::instance().show(ui::strings::kNoteSaveFail, 1500);
        self->request_repaint();
        return;
    }

    FILE* f = std::fopen(path, "wb");
    if (f == nullptr)
    {
        overlays::Toast::instance().show(ui::strings::kNoteSaveFail, 1500);
        self->request_repaint();
        return;
    }
    std::fclose(f);

    (void)std::strncpy(self->path_, path, sizeof(self->path_) - 1);
    self->path_[sizeof(self->path_) - 1] = '\0';
    self->title_from_path(self->path_, self->title_buf_, sizeof(self->title_buf_));
    self->body_[0]      = '\0';
    self->dirty_        = false;
    self->scroll_line_  = 0;
    self->view_         = View::Edit;
    self->request_repaint();
    self->open_append_keyboard();
}

void NotepadApp::on_append_kb_done(const char* text, void* user)
{
    auto* self = static_cast<NotepadApp*>(user);
    if (self == nullptr)
        return;
    self->kb_mode_ = KbMode::None;
    if (text != nullptr && text[0] != '\0')
        self->append_text(text);
    self->request_repaint();
}

void NotepadApp::open_append_keyboard()
{
    kb_mode_ = KbMode::Append;
    overlays::KeyboardConfig kc{};
    kc.max_len = 63;
    kc.title   = ui::strings::kNoteInput;
    overlays::Keyboard::instance().open(kc, on_append_kb_done, this);
}

void NotepadApp::fill_row(uint8_t idx, ui::widgets::RowStyle& rs)
{
    if (idx == 0)
    {
        rs.label        = ui::strings::kNoteNew;
        rs.show_chevron = true;
        return;
    }

    const uint8_t note_idx = static_cast<uint8_t>(idx - 1);
    if (note_idx >= count_)
    {
        rs.label = "";
        return;
    }

    const NoteItem& item = notes_[note_idx];
    rs.label             = item.title;
    rs.show_chevron      = true;
    gfx::format_file_size(item.size, size_buf_[note_idx], sizeof(size_buf_[note_idx]));
    rs.value             = size_buf_[note_idx];
}

void NotepadApp::on_row_tap(uint8_t idx)
{
    if (idx == 0)
    {
        start_new_note();
        return;
    }
    open_note(static_cast<uint8_t>(idx - 1));
}

core::Rect NotepadApp::edit_body_rect()
{
    return core::Rect{
        0, static_cast<int16_t>(Theme::kListStartY), Theme::kScreenW,
        static_cast<uint16_t>(Theme::kScreenH - Theme::kListStartY - kActionBarH)};
}

core::Rect NotepadApp::action_bar_rect()
{
    return core::Rect{
        0, static_cast<int16_t>(Theme::kScreenH - kActionBarH), Theme::kScreenW, kActionBarH};
}

core::Rect NotepadApp::action_btn_rect(uint8_t idx)
{
    const core::Rect bar = action_bar_rect();
    if (idx >= kActionBtnNum)
        return {};
    const uint16_t cw = static_cast<uint16_t>(bar.w / kActionBtnNum);
    const int16_t  x  = static_cast<int16_t>(bar.x + idx * cw);
    const uint16_t w  = (idx + 1 == kActionBtnNum)
        ? static_cast<uint16_t>(bar.right() - x) : cw;
    return core::Rect{x, bar.y, w, bar.h};
}

core::Rect NotepadApp::clear_dialog_rect()
{
    const int16_t x = static_cast<int16_t>((Theme::kScreenW - kClearDlgW) / 2);
    const int16_t y = static_cast<int16_t>((Theme::kScreenH - kClearDlgH) / 2);
    return core::Rect{x, y, kClearDlgW, kClearDlgH};
}

core::Rect NotepadApp::clear_dialog_close_rect()
{
    const core::Rect dlg = clear_dialog_rect();
    return core::Rect{
        static_cast<int16_t>(dlg.right() - kCloseBtnSize - kCloseBtnPad),
        static_cast<int16_t>(dlg.y + kCloseBtnPad),
        kCloseBtnSize, kCloseBtnSize};
}

core::Rect NotepadApp::clear_dialog_yes_rect()
{
    const core::Rect dlg = clear_dialog_rect();
    const uint16_t   half = static_cast<uint16_t>(dlg.w / 2);
    return core::Rect{
        dlg.x, static_cast<int16_t>(dlg.bottom() - kClearBtnH), half, kClearBtnH};
}

core::Rect NotepadApp::clear_dialog_no_rect()
{
    const core::Rect dlg = clear_dialog_rect();
    const uint16_t   half = static_cast<uint16_t>(dlg.w / 2);
    return core::Rect{
        static_cast<int16_t>(dlg.x + half),
        static_cast<int16_t>(dlg.bottom() - kClearBtnH), half, kClearBtnH};
}

void NotepadApp::show_clear_dialog()
{
    clear_dialog_ = true;
    request_repaint();
}

void NotepadApp::close_clear_dialog()
{
    if (!clear_dialog_)
        return;
    clear_dialog_ = false;
    request_repaint();
}

void NotepadApp::paint_clear_dialog(gfx::Canvas& c)
{
    const core::Rect dlg = clear_dialog_rect();
    c.fill(dlg, gfx::Ink::White);
    c.rect(dlg, 2);

    gfx::Canvas::TextStyle ts{};
    ts.size_px = Theme::kFontBody;
    ts.h       = gfx::HAlign::Center;
    ts.v       = gfx::VAlign::Middle;
    ts.padding = 2;

    const uint16_t msg_w = static_cast<uint16_t>(
        dlg.w - kCloseBtnSize - kCloseBtnPad * 2);
    const core::Rect msg{
        dlg.x, static_cast<int16_t>(dlg.y + 4), msg_w,
        static_cast<uint16_t>(dlg.h - kClearBtnH - 6)};
    c.text_in(msg, ui::strings::kNoteClearAsk, ts);

    const core::Rect close_btn = clear_dialog_close_rect();
    c.rect(close_btn, 1);
    const int16_t ix = static_cast<int16_t>(
        close_btn.x + (close_btn.w - kCloseIconSize) / 2);
    const int16_t iy = static_cast<int16_t>(
        close_btn.y + (close_btn.h - kCloseIconSize) / 2);
    c.glyph(ix, iy, gfx::icon::kFaTimes, kCloseIconSize, gfx::FontFace::Icon);

    c.rect(clear_dialog_yes_rect(), 1);
    c.rect(clear_dialog_no_rect(), 1);
    c.text_in(clear_dialog_yes_rect(), ui::strings::kDrawYes, ts);
    c.text_in(clear_dialog_no_rect(), ui::strings::kDrawNo, ts);
    c.vline(static_cast<int16_t>(dlg.x + dlg.w / 2),
            static_cast<int16_t>(dlg.bottom() - kClearBtnH), kClearBtnH);
}

void NotepadApp::paint_action_bar(gfx::Canvas& c)
{
    const core::Rect bar = action_bar_rect();
    c.hline(bar.x, bar.y, bar.w);

    static const char* const kLabels[kActionBtnNum] = {
        ui::strings::kNoteInput,
        ui::strings::kNoteDelLine,
        ui::strings::kNoteClear,
        ui::strings::kNoteSave,
    };

    gfx::Canvas::TextStyle ts{};
    ts.size_px = Theme::kFontSmall;
    ts.h       = gfx::HAlign::Center;
    ts.v       = gfx::VAlign::Middle;
    for (uint8_t i = 0; i < kActionBtnNum; ++i)
    {
        if (i > 0)
        {
            const core::Rect btn = action_btn_rect(i);
            c.vline(btn.x, btn.y, btn.h);
        }
        c.text_in(action_btn_rect(i), kLabels[i], ts);
    }
}

void NotepadApp::paint_edit(gfx::Canvas& c)
{
    paint_action_bar(c);

    const core::Rect body = edit_body_rect();
    gfx::LineSlice   lines[kWrapLines]{};
    const uint8_t    line_count = gfx::wrap_text(
        body_, Theme::kFontBody, body.w, kWrapLines, lines, gfx::FontFace::Text);

    const uint8_t line_h = static_cast<uint8_t>(Theme::kFontBody + 4);
    const uint8_t max_visible =
        static_cast<uint8_t>((body.h + line_h - 1) / line_h);

    gfx::Canvas::TextStyle ts{};
    ts.size_px = Theme::kFontBody;
    ts.h       = gfx::HAlign::Left;
    ts.v       = gfx::VAlign::Top;

    if (line_count == 0 || body_[0] == '\0')
    {
        ts.h = gfx::HAlign::Center;
        ts.v = gfx::VAlign::Middle;
        c.text_in(body, ui::strings::kNoteEditHint, ts);
        return;
    }

    for (uint8_t i = 0; i < max_visible; ++i)
    {
        const uint8_t li = static_cast<uint8_t>(scroll_line_ + i);
        if (li >= line_count)
            break;
        const core::Rect row{
            body.x, static_cast<int16_t>(body.y + i * line_h), body.w, line_h};
        char line_buf[128]{};
        const size_t len = static_cast<size_t>(lines[li].end - lines[li].begin);
        const size_t cap = (sizeof(line_buf) < len + 1) ? sizeof(line_buf) - 1 : len;
        if (lines[li].begin != nullptr && cap > 0)
        {
            (void)std::strncpy(line_buf, lines[li].begin, cap);
            line_buf[cap] = '\0';
            c.text_in(row, line_buf, ts);
        }
    }
}

void NotepadApp::paint(gfx::Canvas& c)
{
    ui::widgets::toolbar(c, title());
    if (view_ == View::List)
    {
        list_.set_total(static_cast<uint8_t>(count_ + 1));
        list_.paint(c);
        return;
    }
    paint_edit(c);
}

void NotepadApp::paint_overlay(gfx::Canvas& c)
{
    if (clear_dialog_)
        paint_clear_dialog(c);
}

shell::InputResult NotepadApp::handle_clear_dialog(int16_t x, int16_t y)
{
    if (clear_dialog_close_rect().contains(x, y) ||
        clear_dialog_no_rect().contains(x, y))
    {
        close_clear_dialog();
        return {true};
    }
    if (clear_dialog_yes_rect().contains(x, y))
    {
        close_clear_dialog();
        clear_body();
        overlays::Toast::instance().show(ui::strings::kNoteCleared, 1200);
        return {true};
    }
    return {true};
}

shell::InputResult NotepadApp::handle_edit_input(int16_t x, int16_t y)
{
    if (action_btn_rect(0).contains(x, y))
    {
        open_append_keyboard();
        return {true};
    }
    if (action_btn_rect(1).contains(x, y))
    {
        delete_last_char();
        return {true};
    }
    if (action_btn_rect(2).contains(x, y))
    {
        if (body_[0] == '\0')
            overlays::Toast::instance().show(ui::strings::kNoteNothingDel, 1200);
        else
            show_clear_dialog();
        return {true};
    }
    if (action_btn_rect(3).contains(x, y))
    {
        save_note();
        overlays::Toast::instance().show(
            dirty_ ? ui::strings::kNoteSaveFail : ui::strings::kNoteSaved, 1500);
        request_repaint();
        return {true};
    }

    const core::Rect body = edit_body_rect();
    if (!body.contains(x, y))
        return {};

    const uint8_t line_h = static_cast<uint8_t>(Theme::kFontBody + 4);
    if (y < body.y + static_cast<int16_t>(line_h))
    {
        if (scroll_line_ > 0)
        {
            --scroll_line_;
            request_repaint();
        }
        return {true};
    }
    if (y > body.bottom() - static_cast<int16_t>(line_h))
    {
        ++scroll_line_;
        request_repaint();
        return {true};
    }
    return {};
}

shell::InputResult NotepadApp::on_input(const ::app::ebook::input::Event& ev)
{
    using ::app::ebook::input::EventType;

    if (overlays::Keyboard::instance().is_open())
        return {};

    const int16_t x = static_cast<int16_t>(ev.x);
    const int16_t y = static_cast<int16_t>(ev.y);

    if (view_ == View::Edit)
    {
        if (clear_dialog_)
        {
            if (ev.type == EventType::Tap)
                return handle_clear_dialog(x, y);
            return {true};
        }

        if (ev.type == EventType::Tap)
        {
            if (ui::widgets::hit_toolbar_back(x, y))
            {
                if (dirty_)
                    save_note();
                view_        = View::List;
                scroll_line_ = 0;
                scan_notes();
                request_repaint();
                return {true};
            }
            return handle_edit_input(x, y);
        }

        if (ev.type == EventType::SwipeUp || ev.type == EventType::SwipeDown)
        {
            gfx::LineSlice lines[kWrapLines]{};
            const core::Rect body = edit_body_rect();
            const uint8_t    line_count = gfx::wrap_text(
                body_, Theme::kFontBody, body.w, kWrapLines, lines, gfx::FontFace::Text);
            const uint8_t line_h = static_cast<uint8_t>(Theme::kFontBody + 4);
            const uint8_t max_visible =
                static_cast<uint8_t>((body.h + line_h - 1) / line_h);
            const uint8_t max_scroll =
                (line_count > max_visible)
                    ? static_cast<uint8_t>(line_count - max_visible)
                    : 0;

            if (ev.type == EventType::SwipeUp)
            {
                if (scroll_line_ < max_scroll)
                    ++scroll_line_;
            }
            else if (scroll_line_ > 0)
            {
                --scroll_line_;
            }
            request_repaint();
            return {true};
        }
        return {};
    }

    if (ev.type == EventType::Tap &&
        ui::widgets::hit_toolbar_back(x, y))
        return {};

    const auto out = list_.handle_input(ev);
    if (out.scroll_changed)
        request_repaint();
    if (out.consumed)
        return {true};
    return {};
}

} // namespace app::ebook::apps::notepad
