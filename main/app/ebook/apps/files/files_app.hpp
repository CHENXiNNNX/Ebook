#pragma once

#include <cstdint>
#include <memory>

#include "apps/app.hpp"
#include "apps/files/vfs_browser.hpp"
#include "system/task/task.hpp"
#include "ui/list_view.hpp"
#include "ui/ui_event.hpp"

namespace app::ebook::apps::files {

class FilesApp : public App
{
  public:
    static FilesApp& instance();

    AppId       id()      const override { return AppId::Files; }
    const char* title()   const override;
    uint32_t    icon_cp() const override;

    void on_enter() override;
    void on_exit()  override;
    void paint(gfx::Canvas& canvas) override;
    void paint_overlay(gfx::Canvas& canvas) override;
    shell::InputResult on_input(const ::app::ebook::input::Event& ev) override;
    void on_ui_event(const ui::UiEvent& ev) override;

  private:
    FilesApp();

    enum class FileDlg : uint8_t
    {
        None = 0,
        Action,
        Info,
        DeleteConfirm,
    };

    enum class OpenKind : uint8_t
    {
        Unsupported = 0,
        Txt,
        Bmp,
        Jpeg,
        Audio,
    };

    void sync_list_total();
    void close_dialog();
    void rebuild_row_cache();
    void on_browse_ready();
    void finish_browse_reload();
    void schedule_browse_reload();
    void cancel_browse_reload();
    void request_scroll_repaint();

    void fill_row(uint8_t idx, ui::widgets::RowStyle& rs);
    void on_row_tap(uint8_t idx);
    bool handle_toolbar_back();

    static OpenKind classify_open(const char* name);
    static const char* type_label(OpenKind kind, const char* name);
    static bool ends_with_ci(const char* name, const char* ext);
    static void display_name(const char* raw, char* out, size_t out_cap, uint16_t max_w);
    static uint32_t row_icon(const VfsBrowser::Entry& e);

    static core::Rect file_dlg_rect(FileDlg dlg);
    static core::Rect file_dlg_close_rect(FileDlg dlg);
    static core::Rect file_action_btn_rect(uint8_t idx);
    static core::Rect file_confirm_btn_rect(bool yes);
    static core::Rect file_info_back_rect();

    void paint_file_dialog(gfx::Canvas& c);
    void paint_dlg_close_btn(gfx::Canvas& c, FileDlg dlg);
    void paint_action_dialog(gfx::Canvas& c);
    void paint_info_dialog(gfx::Canvas& c);
    void paint_delete_dialog(gfx::Canvas& c);

    shell::InputResult handle_dialog_tap(int16_t x, int16_t y);
    void show_file_action(uint8_t entry_idx);
    void open_selected_file();
    void delete_selected_file();
    void build_info_lines();

    VfsBrowser browser_;
    ui::ListView list_;

    FileDlg          dlg_{FileDlg::None};
    uint8_t          sel_idx_{0};
    char             sel_path_[VfsBrowser::kPathCap]{};
    VfsBrowser::Entry sel_entry_{};
    bool             sel_is_system_{false};

    bool     loading_{false};
    int64_t  last_scroll_repaint_ms_{0};
    std::unique_ptr<::app::sys::task::Task> reload_task_;

    char row_label_[VfsBrowser::kMaxEntries][VfsBrowser::kNameCap]{};
    char row_value_[VfsBrowser::kMaxEntries][16]{};
    uint32_t row_icon_cp_[VfsBrowser::kMaxEntries]{};

    char dlg_title_[40]{};
    char info_lines_[4][64]{};
    mutable char title_buf_[32]{};
};

} // namespace app::ebook::apps::files
