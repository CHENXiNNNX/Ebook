#pragma once

#include <cstdint>

#include "apps/app_id.hpp"

namespace app::ebook::router {

enum class ShellPage : uint8_t
{
    Lock = 0,
    Home,
    AppGrid,
    AppHost,
};

enum class OverlayId : uint8_t
{
    None = 0,
    ControlCenter,
    Keyboard,
    Toast,
};

struct PageId
{
    enum class Zone : uint8_t
    {
        Shell,
        App,
        Overlay,
    } zone{Zone::Shell};

    ShellPage  shell{ShellPage::Lock};
    apps::AppId app{apps::AppId::None};
    uint16_t   app_page{0};
    OverlayId  overlay{OverlayId::None};

    bool operator==(const PageId& o) const
    {
        return zone == o.zone && shell == o.shell && app == o.app &&
               app_page == o.app_page && overlay == o.overlay;
    }
};

constexpr PageId page_shell(ShellPage p)
{
    PageId id{};
    id.zone  = PageId::Zone::Shell;
    id.shell = p;
    return id;
}

constexpr PageId page_app(apps::AppId app, uint16_t sub = 0)
{
    PageId id{};
    id.zone      = PageId::Zone::App;
    id.app       = app;
    id.app_page  = sub;
    return id;
}

constexpr PageId page_overlay(OverlayId o)
{
    PageId id{};
    id.zone    = PageId::Zone::Overlay;
    id.overlay = o;
    return id;
}

const char* page_name(const PageId& id);

} // namespace app::ebook::router
