#pragma once

#include <cstdint>

#include "apps/app.hpp"
#include "apps/reader/book_shelf.hpp"
#include "apps/reader/reader_layout.hpp"
#include "apps/reader/reader_menu.hpp"
#include "apps/reader/txt_book.hpp"
#include "shell/page.hpp"
#include "ui/list_view.hpp"

namespace app::ebook::apps::reader {

enum class ReadRefreshMode : uint8_t
{
    Clear  = 0,
    Normal = 1,
    Fast   = 2,
};

/** @brief 阅读器：书架 → 阅读（顶栏 + 底栏菜单） */
class ReaderApp : public App
{
  public:
    static ReaderApp& instance();

    /** 主页「继续阅读」：下次 on_enter 直达上次书籍 */
    static void request_resume_on_enter();
    /** 文件管理「打开」：下次 on_enter 按路径开书 */
    static void request_open_on_enter(const char* path);

    AppId       id()      const override { return AppId::Reader; }
    const char* title()   const override;
    uint32_t    icon_cp() const override;

    bool wants_status_bar() const override;

    void on_enter() override;
    void on_exit()  override;
    void on_ui_event(const ui::UiEvent& ev) override;
    void paint(gfx::Canvas& canvas) override;
    shell::InputResult on_input(const ::app::ebook::input::Event& ev) override;
    bool on_semantic_back() override;

    void toggle_reading_menu();

  private:
    ReaderApp();

    enum class Screen : uint8_t
    {
        Shelf,
        Opening,
        Reading,
    };

    void after_page_turn();
    void save_progress();
    void restore_progress();
    void return_to_shelf();
    void go_lock_screen();

    bool open_book(uint8_t shelf_idx);
    bool open_book_by_path(const char* path);
    bool try_resume_saved_book();

    void cycle_font_scale(int dir);
    void cycle_refresh_mode(int dir);
    void apply_font_change();
    void open_page_jump();
    static void on_page_jump_done(const char* text, void* user);

    void go_next_page();
    void go_prev_page();
    void go_page_relative(int delta);
    void go_to_page(uint16_t page_index);

    void paint_shelf(gfx::Canvas& c);
    void paint_opening(gfx::Canvas& c);
    void paint_reading(gfx::Canvas& c);
    void paint_top_bar(gfx::Canvas& c);

    void fill_shelf_row(uint8_t idx, ui::widgets::RowStyle& rs);
    void on_shelf_tap(uint8_t idx);
    void handle_menu_hit(MenuHit hit);

    bool handle_reading_tap(int16_t x, int16_t y);
    bool in_reading_content(int16_t x, int16_t y) const;

    const char* font_label() const;
    const char* refresh_label() const;
    ReaderLayout layout() const;

    static bool s_resume_on_enter_;
    static bool s_open_on_enter_;
    static char s_open_path_[96];

    Screen       screen_{Screen::Shelf};
    BookShelf    shelf_;
    TxtBook      book_;
    ui::ListView list_;

    uint8_t           font_scale_{1};
    uint8_t           refresh_mode_{static_cast<uint8_t>(ReadRefreshMode::Normal)};
    /** 本次阅读累计翻页次数（用于分级刷新） */
    uint16_t          turn_count_{0};
    bool              menu_visible_{false};

    uint8_t           tick_hour_{0xFF};
    uint8_t           tick_minute_{0xFF};
    uint8_t           tick_battery_{0xFF};

    char open_title_[48]{};
    char open_path_[96]{};
    char row_title_[48]{};
    char row_value_[16]{};
    char jump_kb_title_[32]{};
    mutable char title_buf_[32]{};
};

} // namespace app::ebook::apps::reader
