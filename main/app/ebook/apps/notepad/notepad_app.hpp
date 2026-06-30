#pragma once

#include <cstddef>
#include <cstdint>

#include "apps/app.hpp"
#include "ui/list_view.hpp"

namespace app::ebook::apps::notepad {

/** @brief 记事本：扫描 notes 目录，列表 + 正文编辑 */
class NotepadApp : public App
{
  public:
    static NotepadApp& instance();

    AppId       id()      const override { return AppId::Notepad; }
    const char* title()   const override;
    uint32_t    icon_cp() const override;

    void on_enter() override;
    void on_exit()  override;
    void paint(gfx::Canvas& canvas) override;
    void paint_overlay(gfx::Canvas& canvas) override;
    shell::InputResult on_input(const ::app::ebook::input::Event& ev) override;

  private:
    NotepadApp();

    enum class View : uint8_t { List = 0, Edit };

    enum class KbMode : uint8_t { None = 0, NewTitle, Append };

    void scan_notes();
    void open_note(uint8_t idx);
    void start_new_note();
    void save_note();
    bool load_note(const char* path);
    bool ensure_notes_dir();
    bool alloc_new_path(char* out, size_t cap);
    void append_text(const char* text);
    void delete_last_char();
    void clear_body();
    void fill_row(uint8_t idx, ui::widgets::RowStyle& rs);
    void on_row_tap(uint8_t idx);

    void paint_edit(gfx::Canvas& c);
    void paint_action_bar(gfx::Canvas& c);
    void paint_clear_dialog(gfx::Canvas& c);
    shell::InputResult handle_edit_input(int16_t x, int16_t y);
    shell::InputResult handle_clear_dialog(int16_t x, int16_t y);

    void show_clear_dialog();
    void close_clear_dialog();

    void open_append_keyboard();
    static void on_title_kb_done(const char* text, void* user);
    static void on_append_kb_done(const char* text, void* user);

    static void title_from_path(const char* path, char* out, size_t out_size);
    static void sanitize_filename(const char* in, char* out, size_t out_size);

    static core::Rect edit_body_rect();
    static core::Rect action_bar_rect();
    static core::Rect action_btn_rect(uint8_t idx);
    static core::Rect clear_dialog_rect();
    static core::Rect clear_dialog_close_rect();
    static core::Rect clear_dialog_yes_rect();
    static core::Rect clear_dialog_no_rect();

    View    view_{View::List};
    KbMode  kb_mode_{KbMode::None};
    uint8_t sel_{0};
    bool    dirty_{false};
    bool    clear_dialog_{false};
    uint8_t scroll_line_{0};

    struct NoteItem
    {
        char     path[128]{};
        char     title[40]{};
        uint32_t size{0};
    };

    static constexpr uint8_t  kMaxNotes   = 32;
    static constexpr uint16_t kBodyCap    = 4096;
    static constexpr uint8_t  kWrapLines  = 48;
    static constexpr uint16_t kActionBarH   = 30;
    static constexpr uint8_t  kActionBtnNum = 4;
    static constexpr uint16_t kClearDlgW    = 152;
    static constexpr uint16_t kClearDlgH    = 76;
    static constexpr uint16_t kClearBtnH    = 28;
    static constexpr uint8_t  kCloseBtnSize = 20;
    static constexpr uint8_t  kCloseIconSize = 14;
    static constexpr uint8_t  kCloseBtnPad   = 2;

    NoteItem notes_[kMaxNotes]{};
    char     size_buf_[kMaxNotes][12]{};
    uint8_t  count_{0};

    char path_[128]{};
    char title_buf_[40]{};
    char body_[kBodyCap]{};

    ui::ListView list_;
};

} // namespace app::ebook::apps::notepad
