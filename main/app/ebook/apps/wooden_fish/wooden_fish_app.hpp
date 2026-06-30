#pragma once

#include <cstdint>

#include <esp_timer.h>

#include "apps/app.hpp"
#include "ui/ui_event.hpp"

namespace app::ebook::apps::wooden_fish {

/** @brief 电子木鱼：敲击动画经 UiBus 投递到 UI 线程刷新 */
class WoodenFishApp : public App
{
  public:
    static WoodenFishApp& instance();

    AppId       id()      const override { return AppId::WoodenFish; }
    const char* title()   const override;
    uint32_t    icon_cp() const override;

    void on_enter() override;
    void on_exit()  override;
    void paint(gfx::Canvas& canvas) override;
    shell::InputResult on_input(const ::app::ebook::input::Event& ev) override;
    void on_ui_event(const ui::UiEvent& ev) override;

  private:
    WoodenFishApp() = default;

    void on_knock();
    void schedule_repaint(bool force);
    void arm_anim_timer();
    void ensure_anim_timer();

    static core::Rect body_rect();
    static core::Rect fish_rect();
    static core::Rect merit_rect();

    struct StickPivot
    {
        int16_t x{0};
        int16_t y{0};
    };

    static StickPivot stick_pivot();
    static void paint_stick(gfx::Canvas& c, bool knocking);

    uint32_t merit_{0};
    int64_t  knock_anim_until_ms_{0};
    int64_t  last_repaint_ms_{0};
    bool     pending_repaint_{false};
    esp_timer_handle_t anim_timer_{nullptr};

    static constexpr uint8_t kFishIconSize  = 56;
    static constexpr uint8_t kStickIconSize = 36;
    static constexpr int64_t kKnockAnimMs   = 160;
    static constexpr int64_t kRepaintMinMs  = 90;
};

} // namespace app::ebook::apps::wooden_fish
