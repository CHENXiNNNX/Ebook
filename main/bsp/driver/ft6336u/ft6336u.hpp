#pragma once

#include <cstddef>
#include <cstdint>

#include <driver/i2c_master.h>

namespace app::bsp::driver::ft6336u {

constexpr uint8_t kI2cAddr = 0x38;

enum TouchEvent : uint8_t
{
    TOUCH_PRESS = 0x00,
    TOUCH_RELEASE = 0x01,
    TOUCH_CONTACT = 0x02,
};

struct TouchPoint
{
    uint16_t x = 0;
    uint16_t y = 0;
    uint8_t event = 0;
    uint8_t id = 0;
};

struct TouchSample
{
    uint8_t count = 0;
    TouchPoint points[2] = {};
};

struct Config
{
    uint32_t i2c_speed_hz = 400'000;
    uint8_t threshold = 30;
    uint8_t active_period = 0x08;
    bool verify_chip = false;

    uint16_t panel_width = 176;
    uint16_t panel_height = 264;

    bool swap_xy = false;
    bool mirror_x = false;
    bool mirror_y = false;

    uint8_t report_int_mode = 0x01;
};

/** FT6336U 触摸（I2C + 坐标变换） */
class Ft6336u
{
  public:
    Ft6336u();
    ~Ft6336u();

    Ft6336u(const Ft6336u&) = delete;
    Ft6336u& operator=(const Ft6336u&) = delete;

    bool init(i2c_master_bus_handle_t bus_handle, const Config* config = nullptr);
    void deinit();

    void apply_transform(bool swap_xy, bool mirror_x, bool mirror_y);

    bool read(TouchSample& sample);
    bool is_touched();

    bool enable_interrupt();
    void disable_interrupt();

    bool wait_interrupt(uint32_t timeout_ms);
    bool poll_interrupt();
    bool touch_active() const;

    bool is_init() const { return initialized_; }
    bool interrupt_enabled() const { return int_enabled_; }

  private:
    enum Reg : uint8_t
    {
        REG_MODE = 0x00,
        REG_TD_STATUS = 0x02,
        REG_THGROUP = 0x80,
        REG_CTRL = 0x86,
        REG_PERIODACTIVE = 0x88,
        REG_CIPHER_LOW = 0xA0,
        REG_CIPHER_HIGH = 0xA3,
        REG_REPORT_INT_MODE = 0xA4,
        REG_FIRMWARE_ID = 0xA6,
        REG_FOCALTECH_ID = 0xA8,
        REG_WORK_STATE = 0xBC,
    };

    static constexpr uint8_t kCipherHighExpected = 0x64;
    static constexpr uint8_t kFocalTechId = 0x11;
    static constexpr uint8_t kWorkStateNormal = 0x01;
    static constexpr uint8_t kTouchRegStride = 6;
    static constexpr size_t kTouchBodyLen = 13;

    void hw_reset();
    void log_chip_info();
    bool probe_chip();
    bool setup_base_regs();
    void map_coord(uint16_t& x, uint16_t& y) const;
    bool write_reg(uint8_t reg, uint8_t val);
    bool read_reg(uint8_t reg, uint8_t* buf, size_t len) const;
    static bool parse_point(const uint8_t* buf, TouchPoint& pt);
    static bool parse_touch_body(const uint8_t* body, TouchSample& sample);

    Config cfg_;
    i2c_master_bus_handle_t bus_handle_;
    i2c_master_dev_handle_t dev_handle_;
    bool initialized_;
    bool int_enabled_;
};

} // namespace app::bsp::driver::ft6336u
