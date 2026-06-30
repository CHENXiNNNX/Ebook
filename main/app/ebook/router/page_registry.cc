#include "router/page_id.hpp"

#include "apps/app_id.hpp"
#include "ui/strings.hpp"

namespace app::ebook::router {

namespace {

const char* shell_name(ShellPage p)
{
    switch (p)
    {
        case ShellPage::Lock:    return "Lock";
        case ShellPage::Home:    return "Home";
        case ShellPage::AppGrid: return "AppGrid";
        case ShellPage::AppHost: return "AppHost";
    }
    return "?";
}

const char* overlay_name(OverlayId o)
{
    switch (o)
    {
        case OverlayId::None:          return "None";
        case OverlayId::ControlCenter: return "ControlCenter";
        case OverlayId::Keyboard:      return "Keyboard";
        case OverlayId::Toast:         return "Toast";
    }
    return "?";
}

} // namespace

const char* page_name(const PageId& id)
{
    switch (id.zone)
    {
        case PageId::Zone::Shell:
            return shell_name(id.shell);
        case PageId::Zone::Overlay:
            return overlay_name(id.overlay);
        case PageId::Zone::App:
            return apps::app_id_name(id.app);
    }
    return "?";
}

} // namespace app::ebook::router
