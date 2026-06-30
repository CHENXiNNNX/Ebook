#include "router/refresh_edges.hpp"

#include "core/log.hpp"

static const char* const TAG = "RefreshEdges";

namespace app::ebook::router {

namespace {

using apps::AppId;

constexpr RefreshIntent kPartialFull = intent_partial_full();

bool edge_match(const RefreshEdge& e, const Transition& t)
{
    return e.from == t.from && e.to == t.to && e.action == t.action;
}

// kEdges[]：Shell / Overlay / App 跳转默认 intent_partial_full()
constexpr RefreshEdge kEdges[] = {
    {page_shell(ShellPage::Lock), page_shell(ShellPage::Lock), NavAction::Replace, kPartialFull},
    {page_shell(ShellPage::Lock), page_shell(ShellPage::Home), NavAction::Forward, kPartialFull},
    {page_shell(ShellPage::Home), page_shell(ShellPage::Lock), NavAction::Replace, kPartialFull},
    {page_overlay(OverlayId::ControlCenter), page_shell(ShellPage::Lock), NavAction::Replace, kPartialFull},
    {page_app(AppId::Reader), page_shell(ShellPage::Lock), NavAction::Replace, kPartialFull},
    {page_shell(ShellPage::Home), page_shell(ShellPage::AppGrid), NavAction::Forward, kPartialFull},
    {page_shell(ShellPage::AppGrid), page_shell(ShellPage::Home), NavAction::Back, kPartialFull},

    {page_shell(ShellPage::Home), page_overlay(OverlayId::ControlCenter), NavAction::Forward, kPartialFull},
    {page_shell(ShellPage::AppGrid), page_overlay(OverlayId::ControlCenter), NavAction::Forward, kPartialFull},
    {page_shell(ShellPage::AppHost), page_overlay(OverlayId::ControlCenter), NavAction::Forward, kPartialFull},
    {page_overlay(OverlayId::ControlCenter), page_shell(ShellPage::Home), NavAction::Back, kPartialFull},
    {page_overlay(OverlayId::ControlCenter), page_shell(ShellPage::AppGrid), NavAction::Back, kPartialFull},
    {page_overlay(OverlayId::ControlCenter), page_shell(ShellPage::AppHost), NavAction::Back, kPartialFull},

    {page_shell(ShellPage::Home), page_app(AppId::Reader), NavAction::Forward, kPartialFull},
    {page_shell(ShellPage::Home), page_app(AppId::Notepad), NavAction::Forward, kPartialFull},
    {page_shell(ShellPage::Home), page_app(AppId::WoodenFish), NavAction::Forward, kPartialFull},
    {page_shell(ShellPage::Home), page_app(AppId::Settings), NavAction::Forward, kPartialFull},

    {page_shell(ShellPage::AppGrid), page_app(AppId::Reader), NavAction::Forward, kPartialFull},
    {page_shell(ShellPage::AppGrid), page_app(AppId::Notepad), NavAction::Forward, kPartialFull},
    {page_shell(ShellPage::AppGrid), page_app(AppId::Gallery), NavAction::Forward, kPartialFull},
    {page_shell(ShellPage::AppGrid), page_app(AppId::Drawing), NavAction::Forward, kPartialFull},
    {page_shell(ShellPage::AppGrid), page_app(AppId::Music), NavAction::Forward, kPartialFull},
    {page_shell(ShellPage::AppGrid), page_app(AppId::Weather), NavAction::Forward, kPartialFull},
    {page_shell(ShellPage::AppGrid), page_app(AppId::Clock), NavAction::Forward, kPartialFull},
    {page_shell(ShellPage::AppGrid), page_app(AppId::Calendar), NavAction::Forward, kPartialFull},
    {page_shell(ShellPage::AppGrid), page_app(AppId::Files), NavAction::Forward, kPartialFull},
    {page_shell(ShellPage::AppGrid), page_app(AppId::WoodenFish), NavAction::Forward, kPartialFull},
    {page_shell(ShellPage::AppGrid), page_app(AppId::Update), NavAction::Forward, kPartialFull},
    {page_shell(ShellPage::AppGrid), page_app(AppId::Settings), NavAction::Forward, kPartialFull},

    {page_app(AppId::Reader), page_shell(ShellPage::Home), NavAction::Back, kPartialFull},
    {page_app(AppId::Notepad), page_shell(ShellPage::Home), NavAction::Back, kPartialFull},
    {page_app(AppId::Gallery), page_shell(ShellPage::Home), NavAction::Back, kPartialFull},
    {page_app(AppId::Drawing), page_shell(ShellPage::Home), NavAction::Back, kPartialFull},
    {page_app(AppId::WoodenFish), page_shell(ShellPage::Home), NavAction::Back, kPartialFull},
    {page_app(AppId::Music), page_shell(ShellPage::Home), NavAction::Back, kPartialFull},
    {page_app(AppId::Weather), page_shell(ShellPage::Home), NavAction::Back, kPartialFull},
    {page_app(AppId::Clock), page_shell(ShellPage::Home), NavAction::Back, kPartialFull},
    {page_app(AppId::Calendar), page_shell(ShellPage::Home), NavAction::Back, kPartialFull},
    {page_app(AppId::Files), page_shell(ShellPage::Home), NavAction::Back, kPartialFull},
    {page_app(AppId::Update), page_shell(ShellPage::Home), NavAction::Back, kPartialFull},
    {page_app(AppId::Settings), page_shell(ShellPage::Home), NavAction::Back, kPartialFull},

    {page_app(AppId::Reader), page_shell(ShellPage::AppGrid), NavAction::Back, kPartialFull},
    {page_app(AppId::Notepad), page_shell(ShellPage::AppGrid), NavAction::Back, kPartialFull},
    {page_app(AppId::Gallery), page_shell(ShellPage::AppGrid), NavAction::Back, kPartialFull},
    {page_app(AppId::Drawing), page_shell(ShellPage::AppGrid), NavAction::Back, kPartialFull},
    {page_app(AppId::WoodenFish), page_shell(ShellPage::AppGrid), NavAction::Back, kPartialFull},
    {page_app(AppId::Music), page_shell(ShellPage::AppGrid), NavAction::Back, kPartialFull},
    {page_app(AppId::Weather), page_shell(ShellPage::AppGrid), NavAction::Back, kPartialFull},
    {page_app(AppId::Clock), page_shell(ShellPage::AppGrid), NavAction::Back, kPartialFull},
    {page_app(AppId::Calendar), page_shell(ShellPage::AppGrid), NavAction::Back, kPartialFull},
    {page_app(AppId::Files), page_shell(ShellPage::AppGrid), NavAction::Back, kPartialFull},
    {page_app(AppId::Update), page_shell(ShellPage::AppGrid), NavAction::Back, kPartialFull},
    {page_app(AppId::Settings), page_shell(ShellPage::AppGrid), NavAction::Back, kPartialFull},
};

} // namespace

RefreshIntent RefreshEdges::fallback()
{
    return kPartialFull;
}

RefreshIntent RefreshEdges::resolve(const Transition& t)
{
    for (const RefreshEdge& e : kEdges)
    {
        if (edge_match(e, t))
            return e.intent;
    }
    EBOOK_LOGW(TAG, "missing edge %s->%s %s, fallback Partial",
               page_name(t.from), page_name(t.to), action_name(t.action));
    return fallback();
}

} // namespace app::ebook::router
