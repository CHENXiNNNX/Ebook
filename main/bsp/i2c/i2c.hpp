#pragma once

#include <cstdint>

#include <driver/gpio.h>
#include <driver/i2c_master.h>

#include "config/config.hpp"

namespace app::bsp::i2c {

struct Config
{
    i2c_port_t port;
    gpio_num_t sda_pin;
    gpio_num_t scl_pin;
    bool enable_internal_pullup;

    Config()
        : port(I2C_NUM_1)
        , sda_pin(config::I2C_SDA)
        , scl_pin(config::I2C_SCL)
        , enable_internal_pullup(true)
    {
    }
};

/** @brief I2C master bus（引用计数共享单 bus） */
class I2C
{
  public:
    I2C();
    ~I2C();

    bool init(const Config* config = nullptr);
    int scan(uint32_t timeout_ms = 200);

    bool is_init() const { return bus_handle_ != nullptr; }

    i2c_master_bus_handle_t get_bus_handle() const { return bus_handle_; }

    void deinit();

  private:
    i2c_master_bus_handle_t bus_handle_;
    Config config_;
    bool initialized_;
};

} // namespace app::bsp::i2c
