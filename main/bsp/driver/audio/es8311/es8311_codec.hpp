#pragma once

#include <cstdint>

#include <driver/i2c_master.h>

namespace app::bsp::driver::audio::es8311 {

constexpr uint8_t kI2cAddr = 0x18;

struct Config
{
    uint32_t sample_rate = 16000;
    uint8_t  channels    = 1;
    uint8_t bits_per_sample = 16;
    bool use_mclk = true;
    i2c_port_t i2c_port = I2C_NUM_1;
    uint8_t i2c_addr = kI2cAddr;
    bool pa_inverted = false;
    uint8_t default_output_volume = 60;
    uint8_t default_input_gain_db = 30;
};

/** ES8311 I2S + I2C（esp_codec_dev）；enable_input/output 延迟打开通路 */
class Es8311Codec
{
  public:
    Es8311Codec();
    ~Es8311Codec();

    bool init(i2c_master_bus_handle_t bus_handle, const Config* config = nullptr);
    void deinit();

    void enable_input(bool enable);
    void enable_output(bool enable);

    void set_output_volume(uint8_t percent);
    void set_input_gain(uint8_t gain_db);
    uint8_t get_output_volume() const;
    uint8_t get_input_gain() const;

    int read(int16_t* dest, int samples);
    int write(const int16_t* data, int samples);

    bool is_init() const { return initialized_; }
    bool is_input_enabled() const { return input_enabled_; }
    bool is_output_enabled() const { return output_enabled_; }

  private:
    bool setup_i2s();
    bool setup_codec_stack();
    void release_dev();
    void release_all();
    void sync_data_path();
    void apply_pa();
    void apply_dev_gain_volume();

    i2c_master_bus_handle_t bus_handle_;
    Config cfg_;

    void* tx_handle_;
    void* rx_handle_;
    const void* data_if_;
    const void* ctrl_if_;
    const void* gpio_if_;
    const void* codec_if_;
    void* dev_;

    bool initialized_;
    bool input_enabled_;
    bool output_enabled_;
    uint8_t output_volume_;
    uint8_t input_gain_;
};

} // namespace app::bsp::driver::audio::es8311
