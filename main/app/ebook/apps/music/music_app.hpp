#pragma once

#include <cstdint>
#include <memory>

#include "apps/app.hpp"
#include "system/task/task.hpp"
#include "ui/list_view.hpp"
#include "ui/ui_event.hpp"

namespace app::ebook::apps::music {

/**
 * @brief 音乐：Library / Player；进度经 SystemHint::MusicProgress 局刷
 */
class MusicApp : public App
{
  public:
    static MusicApp& instance();

    static void request_play_on_enter(const char* path);

    AppId       id()      const override { return AppId::Music; }
    const char* title()   const override;
    uint32_t    icon_cp() const override;

    void on_enter() override;
    void on_exit()  override;

    void paint(gfx::Canvas& canvas) override;
    shell::InputResult on_input(const ::app::ebook::input::Event& ev) override;
    void on_ui_event(const ui::UiEvent& ev) override;

  private:
    MusicApp();

    enum class View : uint8_t { Library = 0, Player };

    void set_view(View v);
    void start_scan();
    void play_index(uint8_t idx);
    void play_adjacent(int8_t delta);

    static core::Rect player_body_rect();
    static core::Rect player_controls_rect();
    static core::Rect ctrl_btn_rect(uint8_t idx);

    void paint_library(gfx::Canvas& c);
    void paint_player(gfx::Canvas& c);

    shell::InputResult handle_player(int16_t x, int16_t y);

    View     view_{View::Library};
    uint8_t  track_idx_{0};
    bool     scanning_{false};

    std::unique_ptr<::app::sys::task::Task> scan_task_;
    ui::ListView list_{};
};

} // namespace app::ebook::apps::music
