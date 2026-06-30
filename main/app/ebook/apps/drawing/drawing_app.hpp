#pragma once

#include <cstdint>

#include "apps/app.hpp"
#include "core/geometry.hpp"

namespace app::ebook::apps::drawing {

class DrawingApp : public App
{
  public:
    static DrawingApp& instance();

    static void request_open_on_enter(const char* path);

    AppId       id()      const override { return AppId::Drawing; }
    const char* title()   const override;
    uint32_t    icon_cp() const override;

    void on_enter() override;
    void on_exit()  override;

    void paint(gfx::Canvas& canvas) override;
    void paint_overlay(gfx::Canvas& canvas) override;
    shell::InputResult on_input(const ::app::ebook::input::Event& ev) override;

  private:
    DrawingApp() = default;

    enum class Tool : uint8_t { Pen = 0, Eraser };

    static core::Rect tool_row_rect();
    static core::Rect canvas_rect();
    static core::Rect tool_btn_rect(uint8_t idx);
    static uint8_t    hit_tool_btn(int16_t x, int16_t y);

    static core::Rect exit_dialog_rect();
    static core::Rect exit_dialog_close_rect();
    static core::Rect exit_btn_rect(bool discard);

    bool    ensure_buffer();
    uint8_t brush_half() const;
    bool    canvas_has_strokes() const;

    void stamp(int16_t cx, int16_t cy);
    void stroke_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1);
    void flush_dirty(bool force);
    void end_stroke();

    void clear_canvas();
    bool save_bmp(char* out_name, uint8_t name_cap);
    bool save_session();
    bool load_session();
    bool load_bmp_file(const char* path);
    void remove_session();
    void reset_canvas_white();

    static bool consume_pending_open(char* path, size_t cap);

    void paint_tool_row(gfx::Canvas& c);
    void paint_exit_dialog(gfx::Canvas& c);
    shell::InputResult handle_tool_tap(uint8_t idx);
    shell::InputResult handle_exit_dialog(int16_t x, int16_t y);
    void show_exit_dialog();
    void finish_exit(bool discard);

    uint8_t*   buf_{nullptr};
    Tool       tool_{Tool::Pen};
    uint8_t    size_idx_{1};
    bool       stroking_{false};
    bool       exit_dialog_{false};
    int16_t    last_x_{0};
    int16_t    last_y_{0};
    core::Rect dirty_{};
    int64_t    last_flush_ms_{0};
};

} // namespace app::ebook::apps::drawing
