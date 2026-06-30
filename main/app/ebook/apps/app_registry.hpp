#pragma once

#include <cstdint>

#include "apps/app.hpp"

namespace app::ebook::apps {

struct AppEntry
{
    AppId       id;
    const char* title;
    uint32_t    icon_cp;
};

class AppRegistry
{
  public:
    static constexpr uint8_t kMaxApps = 16;

    static AppRegistry& instance();

    void register_app(App* app);
    void register_defaults();

    App*       find(AppId id);
    const App* find(AppId id) const;

    bool open(AppId id, uint16_t page = 0);
    void close_active();

    App*      active() { return active_; }
    AppId     active_id() const { return active_ != nullptr ? active_->id() : AppId::None; }
    uint16_t  active_page() const { return active_page_; }
    bool      is_active() const { return active_ != nullptr; }

    uint8_t  entry_count() const { return count_; }
    AppEntry entry_at(uint8_t idx) const;

  private:
    AppRegistry() = default;

    App*     apps_[kMaxApps]{};
    uint8_t  count_{0};
    App*     active_{nullptr};
    uint16_t active_page_{0};
};

} // namespace app::ebook::apps
