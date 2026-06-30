#pragma once

#include "router/refresh_intent.hpp"
#include "router/transition.hpp"

namespace app::ebook::router {

/** 跳转刷新边表项（见 refresh_edges.cc 中 kEdges[]） */
struct RefreshEdge
{
    PageId        from;
    PageId        to;
    NavAction     action;
    RefreshIntent intent;
};

/** @brief from→to 边表查表；缺省 fallback Partial */
class RefreshEdges
{
  public:
    static RefreshIntent resolve(const Transition& t);
    static RefreshIntent fallback();
};

} // namespace app::ebook::router
