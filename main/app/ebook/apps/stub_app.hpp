#pragma once

#include "apps/app.hpp"

namespace app::ebook::apps {

class StubApp : public App
{
  public:
    constexpr StubApp(AppId id, const char* title, uint32_t icon_cp)
        : id_(id), title_(title), icon_cp_(icon_cp) {}

    AppId       id()      const override { return id_; }
    const char* title()   const override { return title_; }
    uint32_t    icon_cp() const override { return icon_cp_; }

    void paint(gfx::Canvas& canvas) override;

  private:
    AppId       id_;
    const char* title_;
    uint32_t    icon_cp_;
};

} // namespace app::ebook::apps
