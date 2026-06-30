#pragma once

#include <cstdint>

#include "apps/app_id.hpp"
#include "router/page_id.hpp"

namespace app::ebook::router {

/** Shell / App / Overlay 路由栈 */
class RouteStack
{
  public:
    static constexpr uint8_t kMaxShellDepth  = 8;
    static constexpr uint8_t kMaxOverlayDepth = 4;

    void reset_shell(ShellPage page);
    bool push_shell(ShellPage page);
    bool pop_shell();
    ShellPage shell_top() const;
    ShellPage shell_below() const;

    bool push_overlay(OverlayId id);
    bool pop_overlay();
    OverlayId overlay_top() const;

    void set_app(apps::AppId app, uint16_t page);
    apps::AppId active_app() const { return active_app_; }
    uint16_t active_app_page() const { return active_app_page_; }
    void clear_app();

    PageId current() const;

  private:
    ShellPage shell_[kMaxShellDepth]{};
    uint8_t   shell_depth_{0};

    OverlayId overlay_[kMaxOverlayDepth]{};
    uint8_t   overlay_depth_{0};

    apps::AppId active_app_{apps::AppId::None};
    uint16_t    active_app_page_{0};
};

} // namespace app::ebook::router
