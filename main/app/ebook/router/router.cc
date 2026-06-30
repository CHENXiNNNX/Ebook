#include "router/router.hpp"

#include "apps/app_registry.hpp"
#include "core/log.hpp"
#include "presenter/frame_request.hpp"
#include "presenter/presenter.hpp"
#include "router/refresh_edges.hpp"
#include "shell/app_grid_page.hpp"

static const char* const TAG = "Router";

namespace app::ebook::router {

Router& Router::instance()
{
    static Router s;
    return s;
}

core::Status Router::submit_transition(const Transition& t, RefreshIntent intent)
{
    presenter::FrameRequest req{};
    req.transition = t;
    req.intent     = intent;
    req.seq        = ++seq_;

    EBOOK_LOGD(TAG, "nav %s->%s %s intent=%s",
               page_name(t.from), page_name(t.to), action_name(t.action),
               waveform_name(intent.waveform));

    return presenter::Presenter::instance().submit(req);
}

core::Status Router::apply_route_change(PageId to, NavAction action)
{
    using apps::AppRegistry;

    switch (action)
    {
        case NavAction::Repaint:
            return core::Status::Ok;

        case NavAction::Replace:
            if (to.zone == PageId::Zone::Shell)
            {
                AppRegistry::instance().close_active();
                stack_.reset_shell(to.shell);
            }
            return core::Status::Ok;

        case NavAction::Back:
            if (stack_.shell_top() == ShellPage::AppHost)
                AppRegistry::instance().close_active();
            if (!stack_.pop_shell())
                return core::Status::InvalidArg;
            if (stack_.shell_top() != ShellPage::AppHost)
                stack_.clear_app();
            return core::Status::Ok;

        case NavAction::Forward:
            if (to.zone == PageId::Zone::Overlay)
                return stack_.push_overlay(to.overlay) ? core::Status::Ok
                                                       : core::Status::OutOfResource;

            if (to.zone == PageId::Zone::App)
            {
                if (!AppRegistry::instance().open(to.app, to.app_page))
                    return core::Status::NotFound;
                if (stack_.shell_top() != ShellPage::AppHost &&
                    !stack_.push_shell(ShellPage::AppHost))
                    return core::Status::OutOfResource;
                stack_.set_app(to.app, to.app_page);
                return core::Status::Ok;
            }

            if (to.zone == PageId::Zone::Shell)
            {
                if (to.shell == ShellPage::AppHost)
                    return core::Status::InvalidArg;
                if (!stack_.push_shell(to.shell))
                    return core::Status::OutOfResource;
                if (to.shell == ShellPage::AppGrid)
                    shell::AppGridPage::instance().on_enter();
                return core::Status::Ok;
            }
            return core::Status::InvalidArg;

        default:
            return core::Status::InvalidArg;
    }
}

core::Status Router::navigate(PageId to,
                               NavAction action,
                               const RefreshIntent* override_intent)
{
    const PageId from = stack_.current();
    Transition t{from, to, action};

    RefreshIntent intent = override_intent != nullptr
                               ? *override_intent
                               : RefreshEdges::resolve(t);

    const core::Status route_st = apply_route_change(to, action);
    if (route_st != core::Status::Ok)
        return route_st;

    return submit_transition(t, intent);
}

core::Status Router::back(const RefreshIntent* override_intent)
{
    if (stack_.overlay_top() != OverlayId::None)
        return close_overlay(override_intent);

    const PageId from = stack_.current();
    PageId to       = page_shell(stack_.shell_below());

    Transition t{from, to, NavAction::Back};
    RefreshIntent intent = override_intent != nullptr
                               ? *override_intent
                               : RefreshEdges::resolve(t);

    const core::Status route_st = apply_route_change(to, NavAction::Back);
    if (route_st != core::Status::Ok)
        return route_st;

    return submit_transition(t, intent);
}

core::Status Router::replace_shell(ShellPage shell,
                                    const RefreshIntent* override_intent)
{
    return navigate(page_shell(shell), NavAction::Replace, override_intent);
}

core::Status Router::repaint(RefreshIntent intent)
{
    const PageId p = stack_.current();
    Transition t{p, p, NavAction::Repaint};
    return submit_transition(t, intent);
}

core::Status Router::open_overlay(OverlayId id,
                                   const RefreshIntent* override_intent)
{
    return navigate(page_overlay(id), NavAction::Forward, override_intent);
}

core::Status Router::close_overlay(const RefreshIntent* override_intent)
{
    if (stack_.overlay_top() == OverlayId::None)
        return core::Status::InvalidArg;

    const PageId from = stack_.current();
    stack_.pop_overlay();
    const PageId to = stack_.current();

    Transition t{from, to, NavAction::Back};
    RefreshIntent intent = override_intent != nullptr
                               ? *override_intent
                               : RefreshEdges::resolve(t);
    return submit_transition(t, intent);
}

} // namespace app::ebook::router
