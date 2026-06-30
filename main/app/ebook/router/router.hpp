#pragma once

#include "core/result.hpp"
#include "router/refresh_intent.hpp"
#include "router/route_stack.hpp"
#include "router/transition.hpp"

namespace app::ebook::router {

/** @brief 页面路由：栈变更 + RefreshEdges 解析 + 提交 Presenter */
class Router
{
  public:
    static Router& instance();

    PageId current() const { return stack_.current(); }
    const RouteStack& stack() const { return stack_; }

    core::Status navigate(PageId to,
                          NavAction action = NavAction::Forward,
                          const RefreshIntent* override_intent = nullptr);

    core::Status back(const RefreshIntent* override_intent = nullptr);
    core::Status replace_shell(ShellPage shell,
                               const RefreshIntent* override_intent = nullptr);
    core::Status repaint(RefreshIntent intent);

    core::Status open_overlay(OverlayId id,
                              const RefreshIntent* override_intent = nullptr);
    core::Status close_overlay(const RefreshIntent* override_intent = nullptr);

  private:
    Router() = default;

    core::Status submit_transition(const Transition& t, RefreshIntent intent);
    core::Status apply_route_change(PageId to, NavAction action);

    RouteStack stack_{};
    uint32_t   seq_{0};
};

} // namespace app::ebook::router
