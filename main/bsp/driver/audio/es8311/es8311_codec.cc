#include "es8311_codec.hpp"

#include <algorithm>
#include <cstddef>

#include "config/config.hpp"

#include <driver/gpio.h>
#include <driver/i2s_std.h>
#include <driver/i2s_types.h>
#include <esp_err.h>
#include <esp_log.h>

#include "audio_codec_if.h"
#include "es8311_codec.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"

static const char* const TAG = "Es8311Codec";

namespace app::bsp::driver::audio::es8311 {

namespace {

constexpr int kI2sPort = 0;
constexpr uint32_t kDmaDescNum = 6;
constexpr uint32_t kDmaFrameNum = 240;

i2s_data_bit_width_t to_data_width(uint8_t bits)
{
    switch (bits)
    {
        case 16:
            return I2S_DATA_BIT_WIDTH_16BIT;
        case 24:
            return I2S_DATA_BIT_WIDTH_24BIT;
        case 32:
            return I2S_DATA_BIT_WIDTH_32BIT;
        default:
            return I2S_DATA_BIT_WIDTH_16BIT;
    }
}

/** esp_codec I2C 配置需 8-bit 写地址，组件内部会 >> 1 */
uint8_t i2c_addr_to_8bit(uint8_t addr_7bit)
{
    return static_cast<uint8_t>(addr_7bit << 1);
}

void destroy_i2s_channel(void*& handle)
{
    if (handle == nullptr)
    {
        return;
    }

    auto* ch = static_cast<i2s_chan_handle_t>(handle);
    const esp_err_t dis = i2s_channel_disable(ch);
    if (dis != ESP_OK && dis != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGW(TAG, "i2s disable: %s", esp_err_to_name(dis));
    }

    const esp_err_t del = i2s_del_channel(ch);
    if (del != ESP_OK)
    {
        ESP_LOGW(TAG, "i2s del_channel: %s", esp_err_to_name(del));
    }
    handle = nullptr;
}

void fill_i2s_std_config(const Config& cfg, i2s_std_config_t& out)
{
    const i2s_data_bit_width_t dbw = to_data_width(cfg.bits_per_sample);

    out = {};
    out.clk_cfg.sample_rate_hz = cfg.sample_rate;
    out.clk_cfg.clk_src = I2S_CLK_SRC_DEFAULT;
    out.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;

    out.slot_cfg.data_bit_width = dbw;
    out.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO;
    out.slot_cfg.slot_mode = I2S_SLOT_MODE_STEREO;
    out.slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;
    out.slot_cfg.ws_width = dbw;
    out.slot_cfg.ws_pol = false;
    out.slot_cfg.bit_shift = true;

    out.gpio_cfg.mclk = config::I2S_MCLK;
    out.gpio_cfg.bclk = config::I2S_SCLK;
    out.gpio_cfg.ws = config::I2S_LRCK;
    out.gpio_cfg.dout = config::I2S_DO;
    out.gpio_cfg.din = config::I2S_DI;
    out.gpio_cfg.invert_flags.mclk_inv = false;
    out.gpio_cfg.invert_flags.bclk_inv = false;
    out.gpio_cfg.invert_flags.ws_inv = false;
}

} // namespace

Es8311Codec::Es8311Codec()
    : bus_handle_(nullptr)
    , cfg_{}
    , tx_handle_(nullptr)
    , rx_handle_(nullptr)
    , data_if_(nullptr)
    , ctrl_if_(nullptr)
    , gpio_if_(nullptr)
    , codec_if_(nullptr)
    , dev_(nullptr)
    , initialized_(false)
    , input_enabled_(false)
    , output_enabled_(false)
    , output_volume_(60)
    , input_gain_(30)
{
}

Es8311Codec::~Es8311Codec()
{
    deinit();
}

void Es8311Codec::release_dev()
{
    if (dev_ == nullptr)
    {
        return;
    }

    auto* handle = static_cast<esp_codec_dev_handle_t>(dev_);
    esp_codec_dev_close(handle);
    esp_codec_dev_delete(handle);
    dev_ = nullptr;
}

void Es8311Codec::release_all()
{
    release_dev();

    if (codec_if_ != nullptr)
    {
        audio_codec_delete_codec_if(static_cast<const audio_codec_if_t*>(codec_if_));
        codec_if_ = nullptr;
    }
    if (ctrl_if_ != nullptr)
    {
        audio_codec_delete_ctrl_if(static_cast<const audio_codec_ctrl_if_t*>(ctrl_if_));
        ctrl_if_ = nullptr;
    }
    if (gpio_if_ != nullptr)
    {
        audio_codec_delete_gpio_if(static_cast<const audio_codec_gpio_if_t*>(gpio_if_));
        gpio_if_ = nullptr;
    }
    if (data_if_ != nullptr)
    {
        audio_codec_delete_data_if(static_cast<const audio_codec_data_if_t*>(data_if_));
        data_if_ = nullptr;
    }

    destroy_i2s_channel(tx_handle_);
    destroy_i2s_channel(rx_handle_);

    bus_handle_ = nullptr;
    initialized_ = false;
    input_enabled_ = false;
    output_enabled_ = false;
}

bool Es8311Codec::setup_i2s()
{
    i2s_chan_config_t chan_cfg = {};
    chan_cfg.id = kI2sPort;
    chan_cfg.role = I2S_ROLE_MASTER;
    chan_cfg.dma_desc_num = kDmaDescNum;
    chan_cfg.dma_frame_num = kDmaFrameNum;
    chan_cfg.auto_clear_after_cb = true;
    chan_cfg.auto_clear_before_cb = false;

    i2s_chan_handle_t tx = nullptr;
    i2s_chan_handle_t rx = nullptr;

    esp_err_t err = i2s_new_channel(&chan_cfg, &tx, &rx);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "i2s_new_channel 失败: %s", esp_err_to_name(err));
        return false;
    }
    tx_handle_ = tx;
    rx_handle_ = rx;

    i2s_std_config_t std_cfg = {};
    fill_i2s_std_config(cfg_, std_cfg);

    err = i2s_channel_init_std_mode(tx, &std_cfg);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "i2s TX init 失败: %s", esp_err_to_name(err));
        return false;
    }
    err = i2s_channel_init_std_mode(rx, &std_cfg);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "i2s RX init 失败: %s", esp_err_to_name(err));
        return false;
    }
    err = i2s_channel_enable(tx);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "i2s TX enable 失败: %s", esp_err_to_name(err));
        return false;
    }
    err = i2s_channel_enable(rx);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "i2s RX enable 失败: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

bool Es8311Codec::setup_codec_stack()
{
    auto* tx = static_cast<i2s_chan_handle_t>(tx_handle_);
    auto* rx = static_cast<i2s_chan_handle_t>(rx_handle_);

    audio_codec_i2s_cfg_t i2s_data_cfg = {};
    i2s_data_cfg.port = static_cast<uint8_t>(kI2sPort);
    i2s_data_cfg.rx_handle = rx;
    i2s_data_cfg.tx_handle = tx;
    i2s_data_cfg.clk_src = 0;

    const audio_codec_data_if_t* data_if = audio_codec_new_i2s_data(&i2s_data_cfg);
    if (data_if == nullptr)
    {
        ESP_LOGE(TAG, "audio_codec_new_i2s_data 失败");
        return false;
    }
    data_if_ = static_cast<const void*>(data_if);

    audio_codec_i2c_cfg_t i2c_cfg = {};
    i2c_cfg.port = static_cast<uint8_t>(cfg_.i2c_port);
    i2c_cfg.addr = i2c_addr_to_8bit(cfg_.i2c_addr);
    i2c_cfg.bus_handle = bus_handle_;

    const audio_codec_ctrl_if_t* ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    if (ctrl_if == nullptr)
    {
        ESP_LOGE(TAG, "audio_codec_new_i2c_ctrl 失败");
        return false;
    }
    ctrl_if_ = static_cast<const void*>(ctrl_if);

    const audio_codec_gpio_if_t* gpio_if = audio_codec_new_gpio();
    if (gpio_if == nullptr)
    {
        ESP_LOGE(TAG, "audio_codec_new_gpio 失败");
        return false;
    }
    gpio_if_ = static_cast<const void*>(gpio_if);

    es8311_codec_cfg_t es8311_cfg = {};
    es8311_cfg.ctrl_if = ctrl_if;
    es8311_cfg.gpio_if = gpio_if;
    es8311_cfg.codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH;
    es8311_cfg.pa_pin = static_cast<int16_t>(config::I2S_PA);
    es8311_cfg.use_mclk = cfg_.use_mclk;
    es8311_cfg.hw_gain.pa_voltage = 5.0F;
    es8311_cfg.hw_gain.codec_dac_voltage = 3.3F;
    es8311_cfg.pa_reverted = cfg_.pa_inverted;

    const audio_codec_if_t* codec_if = es8311_codec_new(&es8311_cfg);
    if (codec_if == nullptr)
    {
        ESP_LOGE(TAG, "es8311_codec_new 失败");
        return false;
    }
    codec_if_ = static_cast<const void*>(codec_if);
    return true;
}

bool Es8311Codec::init(i2c_master_bus_handle_t bus_handle, const Config* config)
{
    if (initialized_)
    {
        return true;
    }
    if (bus_handle == nullptr)
    {
        ESP_LOGE(TAG, "bus_handle 为空");
        return false;
    }

    cfg_ = (config != nullptr) ? *config : Config();
    bus_handle_ = bus_handle;
    output_volume_ = cfg_.default_output_volume;
    input_gain_ = cfg_.default_input_gain_db;

    if (cfg_.bits_per_sample != 16)
    {
        ESP_LOGW(TAG, "当前仅验证 16-bit，将按 16-bit 运行");
    }

    if (!setup_i2s())
    {
        release_all();
        return false;
    }
    if (!setup_codec_stack())
    {
        release_all();
        return false;
    }

    initialized_ = true;
    ESP_LOGI(TAG, "OK  %u Hz  ch=%u  I2C=0x%02X  I2S=%d",
             static_cast<unsigned>(cfg_.sample_rate),
             static_cast<unsigned>(cfg_.channels),
             static_cast<unsigned>(cfg_.i2c_addr), kI2sPort);
    return true;
}

void Es8311Codec::deinit()
{
    if (!initialized_)
    {
        return;
    }
    release_all();
}

void Es8311Codec::apply_dev_gain_volume()
{
    if (dev_ == nullptr)
    {
        return;
    }

    auto* handle = static_cast<esp_codec_dev_handle_t>(dev_);

    esp_err_t err = esp_codec_dev_set_in_gain(handle, static_cast<float>(input_gain_));
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "set_in_gain 失败: %s", esp_err_to_name(err));
    }
    err = esp_codec_dev_set_out_vol(handle, static_cast<float>(output_volume_));
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "set_out_vol 失败: %s", esp_err_to_name(err));
    }
}

void Es8311Codec::apply_pa()
{
    if (config::I2S_PA == GPIO_NUM_NC)
    {
        return;
    }

    int level = output_enabled_ ? 1 : 0;
    if (cfg_.pa_inverted)
    {
        level = (level != 0) ? 0 : 1;
    }
    gpio_set_level(config::I2S_PA, level);
}

void Es8311Codec::sync_data_path()
{
    const bool need_dev = input_enabled_ || output_enabled_;

    if (need_dev && dev_ == nullptr)
    {
        esp_codec_dev_cfg_t dev_cfg = {};
        dev_cfg.dev_type = ESP_CODEC_DEV_TYPE_IN_OUT;
        dev_cfg.codec_if = static_cast<const audio_codec_if_t*>(codec_if_);
        dev_cfg.data_if = static_cast<const audio_codec_data_if_t*>(data_if_);

        esp_codec_dev_handle_t handle = esp_codec_dev_new(&dev_cfg);
        if (handle == nullptr)
        {
            ESP_LOGE(TAG, "esp_codec_dev_new 失败");
            return;
        }

        esp_codec_dev_sample_info_t fs = {};
        fs.bits_per_sample = cfg_.bits_per_sample;
        fs.channel         = cfg_.channels;
        fs.channel_mask    = 0;
        fs.sample_rate     = cfg_.sample_rate;
        fs.mclk_multiple   = 0;

        esp_err_t err = esp_codec_dev_open(handle, &fs);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "esp_codec_dev_open 失败: %s", esp_err_to_name(err));
            esp_codec_dev_delete(handle);
            return;
        }

        dev_ = handle;
        apply_dev_gain_volume();
    }
    else if (!need_dev && dev_ != nullptr)
    {
        release_dev();
    }

    apply_pa();
}

void Es8311Codec::enable_input(bool enable)
{
    if (!initialized_ || enable == input_enabled_)
    {
        return;
    }
    input_enabled_ = enable;
    sync_data_path();
}

void Es8311Codec::enable_output(bool enable)
{
    if (!initialized_ || enable == output_enabled_)
    {
        return;
    }
    output_enabled_ = enable;
    sync_data_path();
}

void Es8311Codec::set_output_volume(uint8_t percent)
{
    output_volume_ = std::min<uint8_t>(percent, static_cast<uint8_t>(100));
    if (dev_ != nullptr)
    {
        auto* handle = static_cast<esp_codec_dev_handle_t>(dev_);
        const esp_err_t err = esp_codec_dev_set_out_vol(handle, static_cast<float>(output_volume_));
        if (err != ESP_OK)
        {
            ESP_LOGW(TAG, "set_out_vol 失败: %s", esp_err_to_name(err));
        }
    }
}

void Es8311Codec::set_input_gain(uint8_t gain_db)
{
    input_gain_ = gain_db;
    if (dev_ != nullptr)
    {
        auto* handle = static_cast<esp_codec_dev_handle_t>(dev_);
        const esp_err_t err = esp_codec_dev_set_in_gain(handle, static_cast<float>(input_gain_));
        if (err != ESP_OK)
        {
            ESP_LOGW(TAG, "set_in_gain 失败: %s", esp_err_to_name(err));
        }
    }
}

uint8_t Es8311Codec::get_output_volume() const
{
    return output_volume_;
}

uint8_t Es8311Codec::get_input_gain() const
{
    return input_gain_;
}

int Es8311Codec::read(int16_t* dest, int samples)
{
    if (dest == nullptr || samples <= 0 || !initialized_ || !input_enabled_ || dev_ == nullptr)
    {
        return 0;
    }

    const int bytes = static_cast<int>(static_cast<size_t>(samples) * sizeof(int16_t));
    const esp_err_t err =
        esp_codec_dev_read(static_cast<esp_codec_dev_handle_t>(dev_), dest, bytes);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "read 失败: %s", esp_err_to_name(err));
        return 0;
    }
    return samples;
}

int Es8311Codec::write(const int16_t* data, int samples)
{
    if (data == nullptr || samples <= 0 || !initialized_ || !output_enabled_ || dev_ == nullptr)
    {
        return 0;
    }

    const int bytes = static_cast<int>(static_cast<size_t>(samples) * sizeof(int16_t));
    const esp_err_t err =
        esp_codec_dev_write(static_cast<esp_codec_dev_handle_t>(dev_),
                            const_cast<void*>(static_cast<const void*>(data)), bytes);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "write 失败: %s", esp_err_to_name(err));
        return 0;
    }
    return samples;
}

} // namespace app::bsp::driver::audio::es8311
