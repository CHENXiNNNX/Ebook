#include "apps/drawing/drawing_app.hpp"

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

#include <esp_heap_caps.h>

#include "bsp/driver/gdey027t91/framebuffer.hpp"
#include "gfx/icon.hpp"
#include "input/input_router.hpp"
#include "overlays/toast.hpp"
#include "ui/strings.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"

namespace app::ebook::apps::drawing {

namespace {

using ui::Theme;
using Fb = ::app::bsp::driver::gdey027t91::Framebuffer;

constexpr uint16_t kToolRowH    = 28;
constexpr uint8_t  kToolBtnNum  = 5;
constexpr uint8_t  kEraserHalf  = 5;
constexpr int64_t  kFlushMs     = 80;
constexpr uint16_t kExitDlgW       = 152;
constexpr uint16_t kExitDlgH       = 76;
constexpr uint16_t kExitBtnH       = 28;
constexpr uint8_t  kCloseBtnSize   = 20;
constexpr uint8_t  kCloseIconSize  = 14;
constexpr uint8_t  kCloseBtnPad    = 2;

constexpr const char* kDrawDirRoot   = "/int/Ebook";
constexpr const char* kDrawDir       = "/int/Ebook/drawings";
constexpr const char* kSessionPath   = "/int/Ebook/drawings/session.bin";

constexpr uint8_t kPenHalf[3] = {0, 1, 2};
constexpr uint8_t kOpenPathCap = 96;

bool s_open_on_enter_ = false;
char s_open_path_[kOpenPathCap]{};

void wr32(uint8_t* p, uint32_t v)
{
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16);
    p[3] = static_cast<uint8_t>(v >> 24);
}

} // namespace

DrawingApp& DrawingApp::instance()
{
    static DrawingApp s;
    return s;
}

const char* DrawingApp::title()   const { return ui::strings::kAppDrawing; }
uint32_t    DrawingApp::icon_cp() const { return gfx::icon::kFaPaintBrush; }

core::Rect DrawingApp::tool_row_rect()
{
    return core::Rect{0, static_cast<int16_t>(Theme::kListStartY),
                      Theme::kScreenW, kToolRowH};
}

core::Rect DrawingApp::canvas_rect()
{
    const int16_t y = static_cast<int16_t>(Theme::kListStartY + kToolRowH);
    return core::Rect{0, y, Theme::kScreenW,
                      static_cast<uint16_t>(Theme::kScreenH - y)};
}

core::Rect DrawingApp::tool_btn_rect(uint8_t idx)
{
    const core::Rect bar = tool_row_rect();
    const int16_t x0 = static_cast<int16_t>((bar.w * idx) / kToolBtnNum);
    const int16_t x1 = static_cast<int16_t>((bar.w * (idx + 1U)) / kToolBtnNum);
    return core::Rect{x0, bar.y, static_cast<uint16_t>(x1 - x0), bar.h};
}

core::Rect DrawingApp::exit_dialog_rect()
{
    const int16_t x = static_cast<int16_t>((Theme::kScreenW - kExitDlgW) / 2);
    const int16_t y = static_cast<int16_t>((Theme::kScreenH - kExitDlgH) / 2);
    return core::Rect{x, y, kExitDlgW, kExitDlgH};
}

core::Rect DrawingApp::exit_dialog_close_rect()
{
    const core::Rect dlg = exit_dialog_rect();
    return core::Rect{
        static_cast<int16_t>(dlg.right() - kCloseBtnSize - kCloseBtnPad),
        static_cast<int16_t>(dlg.y + kCloseBtnPad),
        kCloseBtnSize, kCloseBtnSize};
}

core::Rect DrawingApp::exit_btn_rect(bool discard)
{
    const core::Rect dlg = exit_dialog_rect();
    const uint16_t half  = static_cast<uint16_t>(dlg.w / 2U);
    const int16_t  x     = discard ? dlg.x : static_cast<int16_t>(dlg.x + half);
    const int16_t  y     = static_cast<int16_t>(dlg.bottom() - kExitBtnH);
    return core::Rect{x, y, half, kExitBtnH};
}

uint8_t DrawingApp::hit_tool_btn(int16_t x, int16_t y)
{
    if (!tool_row_rect().contains(x, y))
        return UINT8_MAX;
    for (uint8_t i = 0; i < kToolBtnNum; ++i)
    {
        if (tool_btn_rect(i).contains(x, y))
            return i;
    }
    return UINT8_MAX;
}

bool DrawingApp::ensure_buffer()
{
    if (buf_ != nullptr)
        return true;

    buf_ = static_cast<uint8_t*>(
        heap_caps_malloc(core::kFbBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (buf_ == nullptr)
        buf_ = static_cast<uint8_t*>(heap_caps_malloc(core::kFbBytes, MALLOC_CAP_8BIT));
    if (buf_ == nullptr)
    {
        overlays::Toast::instance().show(ui::strings::kDrawNoMem, 1500);
        return false;
    }
    std::memset(buf_, 0xFF, core::kFbBytes);
    return true;
}

void DrawingApp::reset_canvas_white()
{
    if (buf_ == nullptr)
        return;
    const core::Rect cv = canvas_rect();
    Fb::fill_rect(buf_, static_cast<uint16_t>(cv.x), static_cast<uint16_t>(cv.y),
                  cv.w, cv.h, false);
}

bool DrawingApp::canvas_has_strokes() const
{
    if (buf_ == nullptr)
        return false;

    const core::Rect cv = canvas_rect();
    for (int16_t y = cv.y; y < cv.bottom(); ++y)
    {
        const uint8_t* row = buf_ + static_cast<uint32_t>(y) * core::kStride;
        for (uint16_t i = 0; i < core::kStride; ++i)
        {
            if (row[i] != 0xFF)
                return true;
        }
    }
    return false;
}

bool DrawingApp::save_session()
{
    if (buf_ == nullptr || !canvas_has_strokes())
        return false;

    (void)::mkdir(kDrawDirRoot, 0775);
    (void)::mkdir(kDrawDir, 0775);

    const core::Rect cv = canvas_rect();

    FILE* f = std::fopen(kSessionPath, "wb");
    if (f == nullptr)
        return false;

    bool ok = true;
    for (int16_t y = cv.y; ok && y < cv.bottom(); ++y)
    {
        const uint8_t* row = buf_ + static_cast<uint32_t>(y) * core::kStride;
        ok = (std::fwrite(row, 1, core::kStride, f) == core::kStride);
    }
    if (!ok)
    {
        std::fclose(f);
        (void)std::remove(kSessionPath);
        return false;
    }
    std::fclose(f);
    return true;
}

bool DrawingApp::load_session()
{
    if (buf_ == nullptr)
        return false;

    FILE* f = std::fopen(kSessionPath, "rb");
    if (f == nullptr)
        return false;

    const core::Rect cv = canvas_rect();
    bool             ok = true;

    for (int16_t y = cv.y; ok && y < cv.bottom(); ++y)
    {
        uint8_t* row = buf_ + static_cast<uint32_t>(y) * core::kStride;
        ok = (std::fread(row, 1, core::kStride, f) == core::kStride);
    }
    std::fclose(f);

    if (!ok)
    {
        reset_canvas_white();
        (void)std::remove(kSessionPath);
        return false;
    }
    return canvas_has_strokes();
}

void DrawingApp::remove_session()
{
    (void)std::remove(kSessionPath);
}

bool DrawingApp::load_bmp_file(const char* path)
{
    if (path == nullptr || !ensure_buffer())
        return false;

    FILE* f = std::fopen(path, "rb");
    if (f == nullptr)
        return false;

    uint8_t hdr[62] = {};
    if (std::fread(hdr, 1, sizeof(hdr), f) != sizeof(hdr))
    {
        std::fclose(f);
        return false;
    }
    if (hdr[0] != 'B' || hdr[1] != 'M' || hdr[28] != 1)
    {
        std::fclose(f);
        return false;
    }

    int32_t w = 0;
    int32_t h = 0;
    std::memcpy(&w, hdr + 18, 4);
    std::memcpy(&h, hdr + 22, 4);
    if (w <= 0 || h == 0)
    {
        std::fclose(f);
        return false;
    }
    if (h < 0)
        h = -h;

    const core::Rect cv = canvas_rect();
    if (static_cast<uint16_t>(w) != cv.w ||
        static_cast<uint16_t>(h) > cv.h)
    {
        std::fclose(f);
        return false;
    }

    reset_canvas_white();

    const uint16_t row_bytes = static_cast<uint16_t>(((static_cast<uint32_t>(w) + 31U) / 32U) * 4U);
    uint8_t        row[64]   = {};
    bool           ok        = true;

    for (int32_t row_i = h - 1; ok && row_i >= 0; --row_i)
    {
        if (std::fread(row, 1, row_bytes, f) != row_bytes)
        {
            ok = false;
            break;
        }
        const int16_t y = static_cast<int16_t>(cv.y + row_i);
        if (y >= cv.bottom())
            continue;
        std::memcpy(buf_ + static_cast<uint32_t>(y) * core::kStride, row, core::kStride);
    }
    std::fclose(f);

    if (!ok)
    {
        reset_canvas_white();
        return false;
    }
    return canvas_has_strokes();
}

void DrawingApp::request_open_on_enter(const char* path)
{
    if (path == nullptr || path[0] == '\0')
        return;
    (void)std::strncpy(s_open_path_, path, kOpenPathCap - 1);
    s_open_path_[kOpenPathCap - 1] = '\0';
    s_open_on_enter_ = true;
}

bool DrawingApp::consume_pending_open(char* path, size_t cap)
{
    if (!s_open_on_enter_ || path == nullptr || cap == 0)
        return false;
    s_open_on_enter_ = false;
    (void)std::strncpy(path, s_open_path_, cap - 1);
    path[cap - 1] = '\0';
    return true;
}

void DrawingApp::on_enter()
{
    exit_dialog_ = false;
    stroking_    = false;
    dirty_       = {};
    if (!ensure_buffer())
        return;

    reset_canvas_white();
    char pending[kOpenPathCap];
    if (consume_pending_open(pending, sizeof(pending)))
    {
        if (!load_bmp_file(pending))
            overlays::Toast::instance().show(ui::strings::kDrawSaveFail, 1500);
    }
    else
    {
        (void)load_session();
    }
    input::InputRouter::get_instance().set_profile(input::Profile::Drawing);
}

void DrawingApp::on_exit()
{
    stroking_    = false;
    exit_dialog_ = false;
    input::InputRouter::get_instance().set_profile(input::Profile::Normal);

    if (!canvas_has_strokes())
        remove_session();
}

uint8_t DrawingApp::brush_half() const
{
    return (tool_ == Tool::Eraser) ? kEraserHalf : kPenHalf[size_idx_ % 3U];
}

void DrawingApp::stamp(int16_t cx, int16_t cy)
{
    if (buf_ == nullptr)
        return;

    const uint8_t half = brush_half();
    const core::Rect r{static_cast<int16_t>(cx - half),
                       static_cast<int16_t>(cy - half),
                       static_cast<uint16_t>(2U * half + 1U),
                       static_cast<uint16_t>(2U * half + 1U)};
    const core::Rect cr = core::Rect::intersect(r, canvas_rect());
    if (cr.empty())
        return;

    Fb::fill_rect(buf_, static_cast<uint16_t>(cr.x), static_cast<uint16_t>(cr.y),
                  cr.w, cr.h, tool_ == Tool::Pen);
    dirty_ = core::Rect::merge(dirty_, cr);
}

void DrawingApp::stroke_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1)
{
    const int16_t dx = static_cast<int16_t>(std::abs(x1 - x0));
    const int16_t dy = static_cast<int16_t>(std::abs(y1 - y0));
    const int16_t sx = (x0 < x1) ? 1 : -1;
    const int16_t sy = (y0 < y1) ? 1 : -1;
    int16_t err = static_cast<int16_t>(dx - dy);

    while (true)
    {
        stamp(x0, y0);
        if (x0 == x1 && y0 == y1)
            break;
        const int16_t e2 = static_cast<int16_t>(2 * err);
        if (e2 > -dy)
        {
            err = static_cast<int16_t>(err - dy);
            x0  = static_cast<int16_t>(x0 + sx);
        }
        if (e2 < dx)
        {
            err = static_cast<int16_t>(err + dx);
            y0  = static_cast<int16_t>(y0 + sy);
        }
    }
}

void DrawingApp::flush_dirty(bool force)
{
    if (dirty_.empty())
        return;
    if (!request_repaint_if_ready(last_flush_ms_, kFlushMs, force))
        return;
    dirty_ = {};
}

void DrawingApp::end_stroke()
{
    if (!stroking_)
        return;
    stroking_ = false;
    flush_dirty(true);
}

void DrawingApp::clear_canvas()
{
    if (!ensure_buffer())
        return;

    reset_canvas_white();
    dirty_ = {};
    remove_session();
    request_repaint();
}

bool DrawingApp::save_bmp(char* out_name, uint8_t name_cap)
{
    if (buf_ == nullptr)
        return false;

    (void)::mkdir(kDrawDirRoot, 0775);
    (void)::mkdir(kDrawDir, 0775);

    char path[64];
    bool slot_found = false;
    for (uint16_t i = 1; i <= 999; ++i)
    {
        (void)std::snprintf(path, sizeof(path), "%s/draw_%03u.bmp",
                            kDrawDir, static_cast<unsigned>(i));
        struct stat st;
        if (::stat(path, &st) != 0)
        {
            slot_found = true;
            break;
        }
    }
    if (!slot_found)
        return false;

    const core::Rect cv        = canvas_rect();
    const uint16_t   row_bytes = static_cast<uint16_t>(((cv.w + 31U) / 32U) * 4U);
    const uint32_t   img_bytes = static_cast<uint32_t>(row_bytes) * cv.h;
    const uint32_t   file_size = 62U + img_bytes;

    uint8_t hdr[62] = {};
    hdr[0] = 'B';
    hdr[1] = 'M';
    wr32(hdr + 2, file_size);
    wr32(hdr + 10, 62U);
    wr32(hdr + 14, 40U);
    wr32(hdr + 18, cv.w);
    wr32(hdr + 22, cv.h);
    hdr[26] = 1;
    hdr[28] = 1;
    wr32(hdr + 34, img_bytes);
    wr32(hdr + 46, 2U);
    hdr[58] = 0xFF;
    hdr[59] = 0xFF;
    hdr[60] = 0xFF;

    FILE* f = std::fopen(path, "wb");
    if (f == nullptr)
        return false;

    bool ok = (std::fwrite(hdr, 1, sizeof(hdr), f) == sizeof(hdr));

    uint8_t row[32] = {};
    for (int16_t y = static_cast<int16_t>(cv.bottom() - 1); ok && y >= cv.y; --y)
    {
        const uint8_t* src = buf_ + static_cast<uint32_t>(y) * core::kStride;
        std::memcpy(row, src, core::kStride);
        std::memset(row + core::kStride, 0,
                    static_cast<size_t>(row_bytes) - core::kStride);
        ok = (std::fwrite(row, 1, row_bytes, f) == row_bytes);
    }
    std::fclose(f);

    if (!ok)
    {
        (void)std::remove(path);
        return false;
    }

    if (out_name != nullptr && name_cap > 0)
    {
        const char* base = std::strrchr(path, '/');
        (void)std::snprintf(out_name, name_cap, "%s",
                            (base != nullptr) ? (base + 1) : path);
    }
    return true;
}

void DrawingApp::paint_tool_row(gfx::Canvas& c)
{
    static constexpr const char* kSizeLbl[3] = {
        ui::strings::kDrawSizeFine,
        ui::strings::kDrawSizeMid,
        ui::strings::kDrawSizeBold,
    };

    const char* labels[kToolBtnNum] = {
        ui::strings::kDrawPen,
        ui::strings::kDrawEraser,
        kSizeLbl[size_idx_ % 3U],
        ui::strings::kDrawClear,
        ui::strings::kDrawSave,
    };

    gfx::Canvas::TextStyle ts{};
    ts.size_px = Theme::kFontBody;
    ts.h       = gfx::HAlign::Center;
    ts.v       = gfx::VAlign::Middle;

    for (uint8_t i = 0; i < kToolBtnNum; ++i)
    {
        const core::Rect btn = tool_btn_rect(i);
        c.rect(btn, 1);
        c.text_in(btn, labels[i], ts);

        const bool active = (i == 0 && tool_ == Tool::Pen) ||
                            (i == 1 && tool_ == Tool::Eraser);
        if (active)
            c.invert(btn);
    }
}

void DrawingApp::paint_exit_dialog(gfx::Canvas& c)
{
    const core::Rect dlg = exit_dialog_rect();
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
        static_cast<uint16_t>(dlg.h - kExitBtnH - 6)};
    c.text_in(msg, ui::strings::kDrawExitAsk, ts);

    const core::Rect close_btn = exit_dialog_close_rect();
    c.rect(close_btn, 1);
    const int16_t ix = static_cast<int16_t>(
        close_btn.x + (close_btn.w - kCloseIconSize) / 2);
    const int16_t iy = static_cast<int16_t>(
        close_btn.y + (close_btn.h - kCloseIconSize) / 2);
    c.glyph(ix, iy, gfx::icon::kFaTimes, kCloseIconSize, gfx::FontFace::Icon);

    const core::Rect yes_btn = exit_btn_rect(true);
    const core::Rect no_btn  = exit_btn_rect(false);
    c.rect(yes_btn, 1);
    c.rect(no_btn, 1);
    c.text_in(yes_btn, ui::strings::kDrawYes, ts);
    c.text_in(no_btn, ui::strings::kDrawNo, ts);
    c.vline(static_cast<int16_t>(dlg.x + dlg.w / 2),
            static_cast<int16_t>(yes_btn.y), kExitBtnH);
}

void DrawingApp::paint(gfx::Canvas& c)
{
    ui::widgets::toolbar(c, title());
    paint_tool_row(c);

    if (buf_ != nullptr && c.fb() != nullptr)
    {
        const core::Rect cr = core::Rect::intersect(canvas_rect(), c.clip());
        if (!cr.empty())
        {
            Fb::blit_rect(c.fb(), buf_, cr.x, cr.y, cr.w, cr.h, core::kStride);
            if (c.dark())
                c.invert(cr);
        }
    }

}

void DrawingApp::paint_overlay(gfx::Canvas& c)
{
    if (exit_dialog_)
        paint_exit_dialog(c);
}

void DrawingApp::show_exit_dialog()
{
    if (stroking_)
    {
        stroking_ = false;
        dirty_    = {};
    }
    exit_dialog_ = true;
    request_repaint();
}

void DrawingApp::finish_exit(bool discard)
{
    exit_dialog_ = false;
    if (discard)
    {
        remove_session();
        reset_canvas_white();
    }
    else if (!save_session())
    {
        overlays::Toast::instance().show(ui::strings::kDrawSaveFail, 1500);
    }
    exit_to_parent();
}

shell::InputResult DrawingApp::handle_exit_dialog(int16_t x, int16_t y)
{
    if (exit_dialog_close_rect().contains(x, y))
    {
        exit_dialog_ = false;
        request_repaint();
        return {true};
    }
    if (exit_btn_rect(true).contains(x, y))
    {
        finish_exit(true);
        return {true};
    }
    if (exit_btn_rect(false).contains(x, y))
    {
        finish_exit(false);
        return {true};
    }
    return {true};
}

shell::InputResult DrawingApp::handle_tool_tap(uint8_t idx)
{
    switch (idx)
    {
        case 0:
            tool_ = Tool::Pen;
            break;
        case 1:
            tool_ = Tool::Eraser;
            break;
        case 2:
            size_idx_ = static_cast<uint8_t>((size_idx_ + 1U) % 3U);
            break;
        case 3:
            clear_canvas();
            return {true};
        case 4:
        {
            char name[20];
            if (save_bmp(name, sizeof(name)))
            {
                char msg[48];
                (void)std::snprintf(msg, sizeof(msg), "%s%s",
                                    ui::strings::kDrawSaved, name);
                overlays::Toast::instance().show(msg, 1800);
            }
            else
            {
                overlays::Toast::instance().show(ui::strings::kDrawSaveFail, 1500);
            }
            return {true};
        }
        default:
            return {true};
    }

    request_repaint();
    return {true};
}

shell::InputResult DrawingApp::on_input(const ::app::ebook::input::Event& ev)
{
    using ::app::ebook::input::EventType;

    const int16_t x     = static_cast<int16_t>(ev.x);
    const int16_t y     = static_cast<int16_t>(ev.y);
    const core::Rect cv = canvas_rect();

    if (exit_dialog_)
    {
        if (ev.type == EventType::Tap)
            return handle_exit_dialog(x, y);
        return {true};
    }

    switch (ev.type)
    {
        case EventType::Press:
            if (cv.contains(x, y) && ensure_buffer())
            {
                stroking_ = true;
                last_x_   = x;
                last_y_   = y;
                stamp(x, y);
                flush_dirty(false);
                return {true};
            }
            return {};

        case EventType::Move:
            if (!stroking_)
                return {};
            stroke_line(last_x_, last_y_, x, y);
            last_x_ = x;
            last_y_ = y;
            flush_dirty(false);
            return {true};

        case EventType::Release:
            if (!stroking_)
                return {};
            stroke_line(last_x_, last_y_, x, y);
            end_stroke();
            return {true};

        case EventType::Tap:
            end_stroke();
            if (ui::widgets::hit_toolbar_back(x, y))
            {
                if (canvas_has_strokes())
                {
                    show_exit_dialog();
                    return {true};
                }
                exit_to_parent();
                return {true};
            }
            {
                const uint8_t idx = hit_tool_btn(x, y);
                if (idx != UINT8_MAX)
                    return handle_tool_tap(idx);
            }
            return {cv.contains(x, y)};

        case EventType::LongPress:
            end_stroke();
            return {true};

        default:
            end_stroke();
            return {};
    }
}

} // namespace app::ebook::apps::drawing
