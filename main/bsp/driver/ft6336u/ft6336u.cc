#include "ft6336u.hpp"

#include <algorithm>
#include <cstring>

#include <driver/gpio.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "config/config.hpp"
#include "system/task/task.hpp"

static const char* const TAG = "FT6336U";

namespace {

SemaphoreHandle_t g_int_sem = nullptr;
bool g_isr_service = false;
int g_int_isr_ref = 0;

void IRAM_ATTR int_isr_handler(void* /*arg*/)
{
    BaseType_t wake = pdFALSE;
    if (g_int_sem != nullptr)
        xSemaphoreGiveFromISR(g_int_sem, &wake);
    if (wake == pdTRUE)
        portYIELD_FROM_ISR();
}

} // namespace

namespace app::bsp::driver::ft6336u {

Ft6336u::Ft6336u()
    : bus_handle_(nullptr)
    , dev_handle_(nullptr)
    , initialized_(false)
    , int_enabled_(false)
{
}

Ft6336u::~Ft6336u()
{
    deinit();
}

void Ft6336u::hw_reset()
{
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = (1ULL << config::FT6336_RST);
    cfg.mode = GPIO_MODE_OUTPUT;
    gpio_config(&cfg);

    gpio_set_level(config::FT6336_RST, 0);
    app::sys::task::TaskMgr::delay_ms(10);
    gpio_set_level(config::FT6336_RST, 1);
    app::sys::task::TaskMgr::delay_ms(300);
}

bool Ft6336u::write_reg(uint8_t reg, uint8_t val)
{
    if (dev_handle_ == nullptr)
        return false;
    const uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(dev_handle_, buf, 2, 1000) == ESP_OK;
}

bool Ft6336u::read_reg(uint8_t reg, uint8_t* buf, size_t len) const
{
    if (dev_handle_ == nullptr || buf == nullptr || len == 0)
        return false;
    return i2c_master_transmit_receive(dev_handle_, &reg, 1, buf, len, 1000) == ESP_OK;
}

void Ft6336u::log_chip_info()
{
    if (dev_handle_ == nullptr)
        return;

    uint8_t cipher_lo = 0;
    uint8_t cipher_hi = 0;
    uint8_t fw = 0;
    uint8_t vendor = 0;
    (void)read_reg(REG_CIPHER_LOW, &cipher_lo, 1);
    (void)read_reg(REG_CIPHER_HIGH, &cipher_hi, 1);
    (void)read_reg(REG_FIRMWARE_ID, &fw, 1);
    (void)read_reg(REG_FOCALTECH_ID, &vendor, 1);

    const char* variant = "unknown";
    if (cipher_lo == 0x02)
        variant = "Ft6336U";
    else if (cipher_lo == 0x01)
        variant = "Ft6336G";

    ESP_LOGI(TAG, "CIPHER=0x%02X%02X (%s) FW=0x%02X FOCALTECH_ID=0x%02X", cipher_hi, cipher_lo,
             variant, fw, vendor);
}

bool Ft6336u::probe_chip()
{
    uint8_t cipher_lo = 0;
    uint8_t cipher_hi = 0;
    uint8_t vendor = 0;

    for (int attempt = 0; attempt < 5; attempt++)
    {
        if (attempt > 0)
            app::sys::task::TaskMgr::delay_ms(20);

        if (!read_reg(REG_CIPHER_HIGH, &cipher_hi, 1) ||
            !read_reg(REG_CIPHER_LOW, &cipher_lo, 1) || !read_reg(REG_FOCALTECH_ID, &vendor, 1))
        {
            continue;
        }

        if (cipher_hi != 0 || cipher_lo != 0 || vendor != 0)
            break;
    }

    const bool id_ok =
        (cipher_hi == kCipherHighExpected && vendor == kFocalTechId &&
         (cipher_lo == 0x01 || cipher_lo == 0x02));
    if (id_ok)
        return true;

    uint8_t td = 0;
    if (read_reg(REG_TD_STATUS, &td, 1))
    {
        ESP_LOGW(TAG, "ID 非典型 CIPHER=0x%02X%02X VENDOR=0x%02X TD=0x%02X", cipher_hi, cipher_lo,
                 vendor, td);
        return true;
    }

    ESP_LOGE(TAG, "探测失败 CIPHER=0x%02X%02X VENDOR=0x%02X", cipher_hi, cipher_lo, vendor);
    return false;
}

bool Ft6336u::setup_base_regs()
{
    return write_reg(REG_MODE, 0x00) && write_reg(REG_THGROUP, cfg_.threshold) &&
           write_reg(REG_CTRL, 0x00) && write_reg(REG_PERIODACTIVE, cfg_.active_period) &&
           write_reg(REG_REPORT_INT_MODE, cfg_.report_int_mode) &&
           write_reg(REG_WORK_STATE, kWorkStateNormal);
}

void Ft6336u::apply_transform(bool swap_xy, bool mirror_x, bool mirror_y)
{
    cfg_.swap_xy = swap_xy;
    cfg_.mirror_x = mirror_x;
    cfg_.mirror_y = mirror_y;
}

void Ft6336u::map_coord(uint16_t& x, uint16_t& y) const
{
    if (cfg_.panel_width == 0 || cfg_.panel_height == 0)
        return;

    int32_t ix = static_cast<int32_t>(x);
    int32_t iy = static_cast<int32_t>(y);

    if (cfg_.swap_xy)
    {
        const int32_t t = ix;
        ix = iy;
        iy = t;
    }

    if (cfg_.mirror_x)
        ix = static_cast<int32_t>(cfg_.panel_width - 1) - ix;
    if (cfg_.mirror_y)
        iy = static_cast<int32_t>(cfg_.panel_height - 1) - iy;

    ix = std::max<int32_t>(0, std::min<int32_t>(ix, cfg_.panel_width - 1));
    iy = std::max<int32_t>(0, std::min<int32_t>(iy, cfg_.panel_height - 1));

    x = static_cast<uint16_t>(ix);
    y = static_cast<uint16_t>(iy);
}

bool Ft6336u::enable_interrupt()
{
    if (!initialized_ || int_enabled_)
        return true;

    if (g_int_sem == nullptr)
    {
        g_int_sem = xSemaphoreCreateBinary();
        if (g_int_sem == nullptr)
        {
            ESP_LOGE(TAG, "INT 信号量创建失败");
            return false;
        }
    }

    if (!g_isr_service)
    {
        if (gpio_install_isr_service(0) != ESP_OK)
        {
            ESP_LOGE(TAG, "gpio_install_isr_service 失败");
            return false;
        }
        g_isr_service = true;
    }

    if (g_int_isr_ref == 0)
    {
        if (gpio_isr_handler_add(config::FT6336_INT, int_isr_handler, nullptr) != ESP_OK)
        {
            ESP_LOGE(TAG, "INT ISR 注册失败");
            return false;
        }
    }
    g_int_isr_ref++;

    if (gpio_set_intr_type(config::FT6336_INT, GPIO_INTR_ANYEDGE) != ESP_OK)
    {
        ESP_LOGE(TAG, "INT 触发类型配置失败");
        disable_interrupt();
        return false;
    }

    int_enabled_ = true;
    ESP_LOGI(TAG, "INT 已启用 GPIO=%d", static_cast<int>(config::FT6336_INT));
    return true;
}

void Ft6336u::disable_interrupt()
{
    if (!int_enabled_)
        return;

    gpio_set_intr_type(config::FT6336_INT, GPIO_INTR_DISABLE);

    if (g_int_isr_ref > 0)
    {
        g_int_isr_ref--;
        if (g_int_isr_ref == 0)
            gpio_isr_handler_remove(config::FT6336_INT);
    }

    int_enabled_ = false;
}

bool Ft6336u::wait_interrupt(uint32_t timeout_ms)
{
    if (!int_enabled_ || g_int_sem == nullptr)
        return false;

    xSemaphoreTake(g_int_sem, 0);
    return xSemaphoreTake(g_int_sem, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

bool Ft6336u::poll_interrupt()
{
    if (!int_enabled_ || g_int_sem == nullptr)
        return false;

    bool got = false;
    while (xSemaphoreTake(g_int_sem, 0) == pdTRUE)
        got = true;
    return got;
}

bool Ft6336u::touch_active() const
{
    return gpio_get_level(config::FT6336_INT) == 0;
}

bool Ft6336u::init(i2c_master_bus_handle_t bus_handle, const Config* config)
{
    if (bus_handle == nullptr)
    {
        ESP_LOGE(TAG, "bus_handle 为空");
        return false;
    }
    if (initialized_)
        return true;

    if (config != nullptr)
        cfg_ = *config;

    bus_handle_ = bus_handle;

    gpio_config_t int_cfg = {};
    int_cfg.pin_bit_mask = (1ULL << config::FT6336_INT);
    int_cfg.mode = GPIO_MODE_INPUT;
    int_cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    int_cfg.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&int_cfg);

    hw_reset();

    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = kI2cAddr;
    dev_cfg.scl_speed_hz = cfg_.i2c_speed_hz;

    if (i2c_master_bus_add_device(bus_handle_, &dev_cfg, &dev_handle_) != ESP_OK)
    {
        ESP_LOGE(TAG, "I2C 设备添加失败");
        return false;
    }

    if (cfg_.verify_chip && !probe_chip())
    {
        deinit();
        return false;
    }

    log_chip_info();

    if (!setup_base_regs())
    {
        ESP_LOGE(TAG, "基础寄存器配置失败");
        deinit();
        return false;
    }

    initialized_ = true;
    ESP_LOGI(TAG, "OK addr=0x%02X mirror=%d/%d swap=%d", kI2cAddr, cfg_.mirror_x ? 1 : 0,
             cfg_.mirror_y ? 1 : 0, cfg_.swap_xy ? 1 : 0);
    return true;
}

void Ft6336u::deinit()
{
    disable_interrupt();

    if (dev_handle_ != nullptr)
    {
        i2c_master_bus_rm_device(dev_handle_);
        dev_handle_ = nullptr;
    }
    bus_handle_ = nullptr;
    initialized_ = false;
}

bool Ft6336u::parse_point(const uint8_t* buf, TouchPoint& pt)
{
    const uint8_t event = (buf[0] >> 6) & 0x03;
    if (event == TOUCH_RELEASE)
        return false;

    pt.x = static_cast<uint16_t>(((buf[0] & 0x0F) << 8) | buf[1]);
    pt.y = static_cast<uint16_t>(((buf[2] & 0x0F) << 8) | buf[3]);
    pt.event = event;
    return true;
}

bool Ft6336u::parse_touch_body(const uint8_t* body, TouchSample& sample)
{
    sample.count = 0;

    const uint8_t td_status = body[0] & 0x0F;
    if (td_status > 2)
        return true;

    for (uint8_t i = 0; i < td_status; i++)
    {
        const uint8_t* p = body + 1 + (static_cast<size_t>(i) * kTouchRegStride);
        TouchPoint pt = {};
        if (!parse_point(p, pt))
            continue;

        pt.id = sample.count;
        sample.points[sample.count++] = pt;
    }

    return true;
}

bool Ft6336u::is_touched()
{
    if (!initialized_)
        return false;

    TouchSample sample = {};
    if (!read(sample))
        return false;
    return sample.count > 0;
}

bool Ft6336u::read(TouchSample& sample)
{
    if (!initialized_)
        return false;

    sample = {};

    uint8_t body[kTouchBodyLen] = {};
    uint8_t body2[kTouchBodyLen] = {};

    if (!read_reg(REG_TD_STATUS, body, sizeof(body)))
        return false;

    if (read_reg(REG_TD_STATUS, body2, sizeof(body2)) &&
        std::memcmp(body, body2, sizeof(body)) != 0)
    {
        std::memcpy(body, body2, sizeof(body));
    }

    if (!parse_touch_body(body, sample))
        return false;

    for (uint8_t i = 0; i < sample.count; i++)
        map_coord(sample.points[i].x, sample.points[i].y);

    return true;
}

} // namespace app::bsp::driver::ft6336u
