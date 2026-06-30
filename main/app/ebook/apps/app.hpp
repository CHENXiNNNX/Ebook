#pragma once

#include <cstdint>

#include "apps/app_id.hpp"
#include "router/page_id.hpp"
#include "router/refresh_intent.hpp"
#include "shell/page.hpp"

namespace app::ebook::apps {

/** App 基类：所有内置应用继承此类 */
class App : public shell::Page
{
  public:
    virtual AppId id() const = 0;
    virtual const char* title() const = 0;
    virtual uint32_t icon_cp() const { return 0; }

    virtual uint16_t default_page() const { return 0; }

    /** 拨码「返回」：已处理则 true（如应用内上一级） */
    virtual bool on_semantic_back() { return false; }

    void on_enter() override {}
    void on_exit() override {}

  protected:
    void navigate_page(uint16_t page,
                       const router::RefreshIntent* override_intent = nullptr);
    void repaint(const router::RefreshIntent& intent);
    void request_repaint();
    void request_repaint(const router::RefreshIntent& intent);

    bool request_repaint_if_ready(int64_t& last_ms, int64_t min_interval_ms, bool force = false);
    void exit_to_parent(const router::RefreshIntent* override_intent = nullptr);
};

} // namespace app::ebook::apps
