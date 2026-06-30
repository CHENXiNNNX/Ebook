#include "apps/app.hpp"

#include "apps/app_registry.hpp"
#include "common/time/time.hpp"
#include "presenter/presenter.hpp"
#include "router/router.hpp"

namespace app::ebook::apps {

void App::navigate_page(uint16_t page, const router::RefreshIntent* override_intent)
{
    (void)router::Router::instance().navigate(router::page_app(id(), page),
                                               router::NavAction::Forward,
                                               override_intent);
}

void App::repaint(const router::RefreshIntent& intent)
{
    (void)router::Router::instance().repaint(intent);
}

void App::request_repaint()
{
    request_repaint(router::intent_partial_full());
}

void App::request_repaint(const router::RefreshIntent& intent)
{
    repaint(intent);
}

void App::exit_to_parent(const router::RefreshIntent* override_intent)
{
    // Router::back 在 AppHost 下会 close_active；路由变更后再合成上屏
    (void)router::Router::instance().back(override_intent);
}

bool App::request_repaint_if_ready(int64_t& last_ms, int64_t min_interval_ms, bool force)
{
    const int64_t now = ::app::common::time::uptime_ms();
    if (!force)
    {
        if ((now - last_ms) < min_interval_ms)
            return false;
        if (!presenter::Presenter::instance().wait_idle(0))
            return false;
    }

    last_ms = now;
    request_repaint();
    return true;
}

} // namespace app::ebook::apps
