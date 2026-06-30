#include "router/route_stack.hpp"

namespace app::ebook::router {

void RouteStack::reset_shell(ShellPage page)
{
    shell_depth_   = 0;
    overlay_depth_ = 0;
    shell_[shell_depth_++] = page;
    clear_app();
}

bool RouteStack::push_shell(ShellPage page)
{
    if (shell_depth_ >= kMaxShellDepth)
        return false;
    shell_[shell_depth_++] = page;
    return true;
}

bool RouteStack::pop_shell()
{
    if (shell_depth_ <= 1)
        return false;
    --shell_depth_;
    if (shell_top() != ShellPage::AppHost)
        clear_app();
    return true;
}

ShellPage RouteStack::shell_top() const
{
    return (shell_depth_ > 0) ? shell_[shell_depth_ - 1] : ShellPage::Lock;
}

ShellPage RouteStack::shell_below() const
{
    return (shell_depth_ >= 2) ? shell_[shell_depth_ - 2] : ShellPage::Lock;
}

bool RouteStack::push_overlay(OverlayId id)
{
    if (overlay_depth_ >= kMaxOverlayDepth)
        return false;
    overlay_[overlay_depth_++] = id;
    return true;
}

bool RouteStack::pop_overlay()
{
    if (overlay_depth_ == 0)
        return false;
    --overlay_depth_;
    return true;
}

OverlayId RouteStack::overlay_top() const
{
    return (overlay_depth_ > 0) ? overlay_[overlay_depth_ - 1] : OverlayId::None;
}

void RouteStack::set_app(apps::AppId app, uint16_t page)
{
    active_app_      = app;
    active_app_page_ = page;
}

void RouteStack::clear_app()
{
    active_app_      = apps::AppId::None;
    active_app_page_ = 0;
}

PageId RouteStack::current() const
{
    if (overlay_top() != OverlayId::None)
        return page_overlay(overlay_top());

    if (shell_top() == ShellPage::AppHost && active_app_ != apps::AppId::None)
        return page_app(active_app_, active_app_page_);

    return page_shell(shell_top());
}

} // namespace app::ebook::router
