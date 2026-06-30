#include "i2c.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <driver/i2c_master.h>

static const char* const TAG = "I2C";

namespace {

i2c_master_bus_handle_t g_bus_handle = nullptr;
int g_bus_ref_count = 0;

} // namespace

namespace app::bsp::i2c {
I2C::I2C()
    : bus_handle_(nullptr)
    , initialized_(false)
{
}

I2C::~I2C()
{
    deinit();
}

bool I2C::init(const Config* config)
{
    if (initialized_)
    {
        ESP_LOGW(TAG, "I2C 实例已初始化");
        return true;
    }

    if (config != nullptr)
    {
        config_ = *config;
    }

    if (g_bus_handle != nullptr)
    {
        bus_handle_ = g_bus_handle;
        initialized_ = true;
        g_bus_ref_count++;
        ESP_LOGI(TAG, "复用已有 I2C bus，引用计数=%d", g_bus_ref_count);
        return true;
    }

    // 配置 I2C master bus
    i2c_master_bus_config_t i2c_bus_cfg = {};
    i2c_bus_cfg.i2c_port = config_.port;
    i2c_bus_cfg.sda_io_num = config_.sda_pin;
    i2c_bus_cfg.scl_io_num = config_.scl_pin;
    i2c_bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    i2c_bus_cfg.glitch_ignore_cnt = 7;
    i2c_bus_cfg.intr_priority = 0;
    i2c_bus_cfg.trans_queue_depth = 0;
    i2c_bus_cfg.flags.enable_internal_pullup = config_.enable_internal_pullup;
    i2c_bus_cfg.flags.allow_pd = false;

    // 创建 I2C master bus
    esp_err_t ret = i2c_new_master_bus(&i2c_bus_cfg, &bus_handle_);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "创建 I2C master bus 失败: %s", esp_err_to_name(ret));
        bus_handle_ = nullptr;
        initialized_ = false;
        return false;
    }

    g_bus_handle = bus_handle_;
    g_bus_ref_count = 1;
    initialized_ = true;
    ESP_LOGI(TAG, "I2C master bus 初始化成功: 端口=%d, sda=%d, scl=%d", config_.port,
             config_.sda_pin, config_.scl_pin);

    return true;
}

int I2C::scan(uint32_t timeout_ms)
{
    if (!initialized_ || bus_handle_ == nullptr)
    {
        ESP_LOGE(TAG, "I2C 未初始化，请先调用 init()");
        return -1;
    }

    ESP_LOGI(TAG, "正在扫描 I2C 总线...");
    printf("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\r\n");

    int device_count = 0;
    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);

    // 扫描所有 I2C 地址 (0x00 - 0x7F)
    for (int i = 0; i < 128; i += 16)
    {
        printf("%02x: ", i);
        for (int j = 0; j < 16; j++)
        {
            uint8_t address = i + j;

            fflush(stdout);

            // 探测设备
            esp_err_t ret = i2c_master_probe(bus_handle_, address, timeout_ticks);
            if (ret == ESP_OK)
            {
                printf("%02x ", address);
                device_count++;
            }
            else if (ret == ESP_ERR_TIMEOUT)
            {
                printf("UU "); // 超时（可能是总线问题）
            }
            else
            {
                printf("-- "); // 无设备
            }
        }
        printf("\r\n");
    }

    if (device_count > 0)
    {
        ESP_LOGI(TAG, "找到 %d 个 I2C 设备", device_count);
    }
    else
    {
        ESP_LOGW(TAG, "未找到 I2C 设备");
    }

    return device_count;
}

void I2C::deinit()
{
    if (!initialized_)
    {
        return;
    }

    initialized_ = false;
    bus_handle_ = nullptr;
    g_bus_ref_count--;

    if (g_bus_ref_count > 0)
    {
        return;
    }

    if (g_bus_handle != nullptr)
    {
        esp_err_t ret = i2c_del_master_bus(g_bus_handle);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "删除 I2C master bus 失败: %s", esp_err_to_name(ret));
        }
        else
        {
            ESP_LOGI(TAG, "I2C master bus 已反初始化");
        }
        g_bus_handle = nullptr;
    }
}
} // namespace app::bsp::i2c
