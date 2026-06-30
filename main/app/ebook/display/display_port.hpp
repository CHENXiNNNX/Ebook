#pragma once

#include "core/geometry.hpp"
#include "core/result.hpp"

#include "bsp/driver/gdey027t91/backlight.hpp"
#include "bsp/driver/gdey027t91/gdey027t91.hpp"

namespace app::ebook::display {

enum class SessionState : uint8_t
{
    Cold,
    PartialReady,
};

/** @brief EPD 上屏适配（Present 波形 + 背光） */
class DisplayPort
{
  public:
    static DisplayPort& instance();

    bool init();
    void deinit();
    bool ready() const { return ready_; }

    uint8_t* framebuffer();
    SessionState session() const;
    void invalidate_session();

    core::Status bootstrap();
    core::Status partial(const core::Rect& rect);
    core::Status fast();
    core::Status full();

    void set_brightness(uint8_t percent);
    uint8_t brightness() const;

  private:
    DisplayPort() = default;

    ::app::bsp::driver::gdey027t91::Gdey027t91 panel_;
    ::app::bsp::driver::gdey027t91::Backlight backlight_;
    bool ready_{false};
};

} // namespace app::ebook::display
