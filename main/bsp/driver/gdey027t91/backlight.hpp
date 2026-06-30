#pragma once

#include <cstdint>

namespace app::bsp::driver::gdey027t91 {

/** PWM 背光 */
class Backlight
{
  public:
    Backlight();
    ~Backlight();

    Backlight(const Backlight&) = delete;
    Backlight& operator=(const Backlight&) = delete;

    bool init();
    void deinit();

    void set_brightness(uint8_t percent);
    void on();
    void off();

    uint8_t brightness() const { return brightness_; }
    bool ready() const { return ready_; }

  private:
    void apply_duty();

    uint8_t brightness_ = 50;
    uint8_t saved_ = 50;
    bool ready_ = false;
};

} // namespace app::bsp::driver::gdey027t91
