#pragma once

#include <cstdint>
#include <mutex>

#include "panel.hpp"

namespace app::bsp::driver::gdey027t91 {

enum class Present : uint8_t
{
    Base = 0,
    Partial,
    Fast,
    Full,
};

/** GDEY027T91 / SSD1680 SPI 驱动 */
class Gdey027t91
{
  public:
    static constexpr uint16_t WIDTH = Panel::WIDTH;
    static constexpr uint16_t HEIGHT = Panel::HEIGHT;
    static constexpr uint16_t STRIDE = Panel::STRIDE;
    static constexpr uint32_t BUFFER_SIZE = Panel::BUFFER_SIZE;

    Gdey027t91();
    ~Gdey027t91();

    Gdey027t91(const Gdey027t91&) = delete;
    Gdey027t91& operator=(const Gdey027t91&) = delete;

    bool init();
    void deinit();

    uint8_t* fb() { return framebuffer_; }
    const uint8_t* fb() const { return framebuffer_; }

    void fill(uint8_t color = 0xFF);

    bool present(Present kind);
    bool present(Present kind, Window win);

    void sleep();
    void wake();

    bool session_ready() const { return base_ok_; }
    /** 丢弃局刷基线，下次 present 前需重新 Base */
    void invalidate_session();
    bool initialized() const { return initialized_; }

    bool is_busy() const;
    bool wait_busy(uint32_t timeout_ms = Panel::BUSY_TIMEOUT_FULL_MS) const;

  private:
    struct Spi;

    class BusGuard
    {
      public:
        explicit BusGuard(Gdey027t91& dev);
        ~BusGuard();
        BusGuard(const BusGuard&) = delete;
        BusGuard& operator=(const BusGuard&) = delete;
        bool ok() const { return acquired_; }

      private:
        Gdey027t91& dev_;
        bool acquired_;
    };

    bool ensure_hw() const;
    void drop_session();
    bool cold_boot();

    bool ensure_power();
    void set_fast_booster_timing();
    bool power_on();
    bool power_off();
    bool trigger_update(uint8_t sequence, uint32_t busy_timeout_ms);

    static void hw_reset();
    bool sw_reset();
    void configure_panel();
    void set_full_ram_entry();
    void set_ram_area(uint8_t x_start, uint8_t x_end, uint8_t y_start, uint8_t y_start_hi,
                      uint8_t y_end, uint8_t y_end_hi);
    void set_ram_cursor(uint16_t x_byte, uint16_t y);
    void apply_window(const Window& win);
    void set_border_waveform(uint8_t value);

    void write_ram_full(uint8_t cmd_id, const uint8_t* src);
    void write_ram_window(uint8_t cmd_id, const Window& win, const uint8_t* region_tl);

    bool present_base();
    bool present_full();
    bool present_fast();
    bool present_partial(const Window& win);

    void cmd(uint8_t c);
    void dat(uint8_t d);
    void dat_bulk(const uint8_t* buf, uint32_t len);
    void cs_assert();
    void cs_release();

    Spi* spi_;
    uint8_t* framebuffer_;
    mutable std::mutex bus_mutex_;
    bool initialized_;
    bool powered_;
    bool base_ok_;
    bool cs_active_;
};

} // namespace app::bsp::driver::gdey027t91
