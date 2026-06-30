#include "gdey027t91.hpp"

#include <algorithm>
#include <cstring>

#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_heap_caps.h>
#include <esp_log.h>

#include "config/config.hpp"
#include "ssd1680.hpp"
#include "system/task/task.hpp"

static const char* const TAG = "GDEY027T91";

namespace app::bsp::driver::gdey027t91 {

using Cmd = ssd1680::Cmd;
using Seq = ssd1680::Seq;
using Param = ssd1680::Param;

struct Gdey027t91::Spi
{
    spi_device_handle_t handle = nullptr;
};

namespace {

constexpr spi_host_device_t kSpiHost = SPI2_HOST;
constexpr int kSpiClockHz = 20'000'000;
constexpr uint32_t kDmaChunkMax = 4096;

} // namespace

Gdey027t91::BusGuard::BusGuard(Gdey027t91& dev)
    : dev_(dev)
    , acquired_(dev.bus_mutex_.try_lock())
{
    if (!acquired_)
        ESP_LOGW(TAG, "总线占用，跳过本次操作");
}

Gdey027t91::BusGuard::~BusGuard()
{
    if (!acquired_)
        return;
    if (dev_.cs_active_)
        dev_.cs_release();
    dev_.bus_mutex_.unlock();
}

Gdey027t91::Gdey027t91()
    : spi_(nullptr)
    , framebuffer_(nullptr)
    , initialized_(false)
    , powered_(false)
    , base_ok_(false)
    , cs_active_(false)
{
}

Gdey027t91::~Gdey027t91()
{
    deinit();
}

bool Gdey027t91::init()
{
    if (initialized_)
        return true;

    gpio_config_t out_cfg = {};
    out_cfg.pin_bit_mask = (1ULL << config::GDEY027T91_DC) | (1ULL << config::GDEY027T91_RST) |
                           (1ULL << config::GDEY027T91_CS);
    out_cfg.mode = GPIO_MODE_OUTPUT;
    if (gpio_config(&out_cfg) != ESP_OK)
    {
        ESP_LOGE(TAG, "GPIO 输出配置失败");
        return false;
    }

    gpio_config_t in_cfg = {};
    in_cfg.pin_bit_mask = (1ULL << config::GDEY027T91_BUSY);
    in_cfg.mode = GPIO_MODE_INPUT;
    if (gpio_config(&in_cfg) != ESP_OK)
    {
        ESP_LOGE(TAG, "BUSY GPIO 配置失败");
        return false;
    }
    gpio_set_level(config::GDEY027T91_CS, 1);

    spi_bus_config_t bus_cfg = {};
    bus_cfg.mosi_io_num = config::GDEY027T91_MOSI;
    bus_cfg.miso_io_num = -1;
    bus_cfg.sclk_io_num = config::GDEY027T91_SCLK;
    bus_cfg.quadwp_io_num = -1;
    bus_cfg.quadhd_io_num = -1;
    bus_cfg.max_transfer_sz = static_cast<int>(kDmaChunkMax);

    const esp_err_t err = spi_bus_initialize(kSpiHost, &bus_cfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE(TAG, "SPI bus init 失败: %s", esp_err_to_name(err));
        return false;
    }

    auto* spi = new Spi();
    spi_device_interface_config_t dev_cfg = {};
    dev_cfg.clock_speed_hz = kSpiClockHz;
    dev_cfg.mode = 0;
    dev_cfg.spics_io_num = -1;
    dev_cfg.queue_size = 1;
    dev_cfg.flags = SPI_DEVICE_HALFDUPLEX;
    if (spi_bus_add_device(kSpiHost, &dev_cfg, &spi->handle) != ESP_OK)
    {
        ESP_LOGE(TAG, "SPI add_device 失败");
        delete spi;
        return false;
    }
    spi_ = spi;

    framebuffer_ = static_cast<uint8_t*>(heap_caps_malloc(BUFFER_SIZE, MALLOC_CAP_DMA));
    if (framebuffer_ == nullptr)
    {
        ESP_LOGE(TAG, "帧缓冲分配失败");
        deinit();
        return false;
    }
    std::memset(framebuffer_, 0xFF, BUFFER_SIZE);

    hw_reset();
    if (!sw_reset())
    {
        deinit();
        return false;
    }
    configure_panel();
    drop_session();

    initialized_ = true;
    ESP_LOGI(TAG, "OK %ux%u SPI=%dMHz", WIDTH, HEIGHT, kSpiClockHz / 1000000);
    return true;
}

void Gdey027t91::deinit()
{
    if (initialized_)
        sleep();

    if (framebuffer_ != nullptr)
    {
        heap_caps_free(framebuffer_);
        framebuffer_ = nullptr;
    }
    if (spi_ != nullptr)
    {
        if (spi_->handle != nullptr)
            spi_bus_remove_device(spi_->handle);
        delete spi_;
        spi_ = nullptr;
    }

    initialized_ = false;
    drop_session();
    cs_active_ = false;
}

void Gdey027t91::fill(uint8_t color)
{
    if (framebuffer_ != nullptr)
        std::memset(framebuffer_, color, BUFFER_SIZE);
}

bool Gdey027t91::ensure_hw() const
{
    if (!initialized_ || spi_ == nullptr || framebuffer_ == nullptr)
    {
        ESP_LOGE(TAG, "设备未初始化");
        return false;
    }
    return true;
}

void Gdey027t91::drop_session()
{
    powered_ = false;
    base_ok_ = false;
}

void Gdey027t91::invalidate_session()
{
    drop_session();
}

void Gdey027t91::cs_assert()
{
    if (cs_active_)
        return;
    gpio_set_level(config::GDEY027T91_CS, 0);
    cs_active_ = true;
}

void Gdey027t91::cs_release()
{
    if (!cs_active_)
        return;
    gpio_set_level(config::GDEY027T91_CS, 1);
    cs_active_ = false;
}

void Gdey027t91::cmd(uint8_t c)
{
    if (spi_ == nullptr || spi_->handle == nullptr)
        return;
    cs_assert();
    gpio_set_level(config::GDEY027T91_DC, 0);

    spi_transaction_t tx = {};
    tx.length = 8;
    tx.tx_buffer = &c;
    spi_device_polling_transmit(spi_->handle, &tx);
}

void Gdey027t91::dat(uint8_t d)
{
    if (spi_ == nullptr || spi_->handle == nullptr)
        return;
    cs_assert();
    gpio_set_level(config::GDEY027T91_DC, 1);

    spi_transaction_t tx = {};
    tx.length = 8;
    tx.tx_buffer = &d;
    spi_device_polling_transmit(spi_->handle, &tx);
}

void Gdey027t91::dat_bulk(const uint8_t* buf, uint32_t len)
{
    if (spi_ == nullptr || spi_->handle == nullptr || buf == nullptr || len == 0)
        return;
    cs_assert();
    gpio_set_level(config::GDEY027T91_DC, 1);

    uint32_t off = 0;
    while (off < len)
    {
        const uint32_t chunk = std::min(len - off, kDmaChunkMax);
        spi_transaction_t tx = {};
        tx.length = chunk * 8;
        tx.tx_buffer = buf + off;
        spi_device_polling_transmit(spi_->handle, &tx);
        off += chunk;
    }
}

void Gdey027t91::hw_reset()
{
    gpio_set_level(config::GDEY027T91_RST, 0);
    ::app::sys::task::TaskMgr::delay_ms(Panel::RESET_SETTLE_MS);
    gpio_set_level(config::GDEY027T91_RST, 1);
    ::app::sys::task::TaskMgr::delay_ms(Panel::RESET_SETTLE_MS);
}

bool Gdey027t91::is_busy() const
{
    return gpio_get_level(config::GDEY027T91_BUSY) == 1;
}

bool Gdey027t91::wait_busy(uint32_t timeout_ms) const
{
    uint32_t elapsed = 0;
    while (is_busy())
    {
        if (elapsed >= timeout_ms)
        {
            ESP_LOGW(TAG, "BUSY 超时 %u ms", static_cast<unsigned>(timeout_ms));
            return false;
        }
        const uint32_t step = (elapsed < 50) ? 1 : 10;
        ::app::sys::task::TaskMgr::delay_ms(step);
        elapsed += step;
    }
    return true;
}

bool Gdey027t91::sw_reset()
{
    cmd(Cmd::kSwReset);
    cs_release();
    if (!wait_busy(Panel::BUSY_TIMEOUT_CMD_MS))
        return false;
    ::app::sys::task::TaskMgr::delay_ms(Panel::RESET_SETTLE_MS);
    return true;
}

void Gdey027t91::set_ram_area(uint8_t x_start, uint8_t x_end, uint8_t y_start, uint8_t y_start_hi,
                              uint8_t y_end, uint8_t y_end_hi)
{
    cmd(Cmd::kSetRamX);
    dat(x_start);
    dat(x_end);
    cmd(Cmd::kSetRamY);
    dat(y_start);
    dat(y_start_hi);
    dat(y_end);
    dat(y_end_hi);
}

void Gdey027t91::set_ram_cursor(uint16_t x_byte, uint16_t y)
{
    cmd(Cmd::kSetRamXCounter);
    dat(static_cast<uint8_t>(x_byte & 0xFF));
    cmd(Cmd::kSetRamYCounter);
    dat(static_cast<uint8_t>(y & 0xFF));
    dat(static_cast<uint8_t>((y >> 8) & 0x01));
}

void Gdey027t91::set_full_ram_entry()
{
    const uint16_t y_end = static_cast<uint16_t>(HEIGHT - 1U);

    cmd(Cmd::kDataEntryMode);
    dat(Param::kDataEntryNormal);

    set_ram_area(0, Panel::RAM_X_END, 0, 0, static_cast<uint8_t>(y_end & 0xFF),
                 static_cast<uint8_t>((y_end >> 8) & 0x01));
    set_ram_cursor(0, 0);
}

void Gdey027t91::set_border_waveform(uint8_t value)
{
    cmd(Cmd::kBorderWaveform);
    dat(value);
    cs_release();
}

void Gdey027t91::configure_panel()
{
    cmd(Cmd::kDriverOutputControl);
    dat(static_cast<uint8_t>((HEIGHT - 1U) & 0xFF));
    dat(static_cast<uint8_t>(((HEIGHT - 1U) >> 8) & 0x01));
    dat(0x00);

    cmd(Cmd::kTempSensor);
    dat(Param::kTempInternal);

    set_border_waveform(Param::kBorderNormal);
    set_full_ram_entry();
    cs_release();
}

void Gdey027t91::apply_window(const Window& win)
{
    const uint16_t x_byte_start = static_cast<uint16_t>(win.x / 8U);
    const uint16_t x_byte_end = static_cast<uint16_t>(((win.x + win.w) / 8U) - 1U);
    const uint16_t y_end = static_cast<uint16_t>(win.y + win.h - 1U);

    set_ram_area(static_cast<uint8_t>(x_byte_start & 0xFF),
                 static_cast<uint8_t>(x_byte_end & 0xFF),
                 static_cast<uint8_t>(win.y & 0xFF),
                 static_cast<uint8_t>((win.y >> 8) & 0x01),
                 static_cast<uint8_t>(y_end & 0xFF),
                 static_cast<uint8_t>((y_end >> 8) & 0x01));
    set_ram_cursor(x_byte_start, win.y);
}

void Gdey027t91::write_ram_full(uint8_t cmd_id, const uint8_t* src)
{
    if (src == nullptr || spi_ == nullptr)
        return;

    set_ram_cursor(0, 0);
    cmd(cmd_id);
    dat_bulk(src, BUFFER_SIZE);
    cs_release();
}

void Gdey027t91::write_ram_window(uint8_t cmd_id, const Window& win, const uint8_t* region_tl)
{
    if (region_tl == nullptr)
        return;

    const uint32_t row_bytes = static_cast<uint32_t>(win.w / 8U);
    cmd(cmd_id);
    for (uint16_t row = 0; row < win.h; row++)
        dat_bulk(region_tl + (static_cast<size_t>(row) * STRIDE), row_bytes);
    cs_release();
}

void Gdey027t91::set_fast_booster_timing()
{
    cmd(0x1A);
    dat(Param::kBoosterTiming);
    cs_release();
}

bool Gdey027t91::power_on()
{
    if (powered_)
        return true;

    cmd(Cmd::kDisplayUpdateCtrl1);
    dat(Seq::kPowerOn);
    cmd(Cmd::kDisplayUpdateCtrl2);
    cs_release();
    if (!wait_busy(Panel::BUSY_TIMEOUT_CMD_MS))
        return false;

    powered_ = true;
    return true;
}

bool Gdey027t91::power_off()
{
    if (!powered_)
        return true;

    cmd(Cmd::kDisplayUpdateCtrl1);
    dat(Seq::kPowerOff);
    cmd(Cmd::kDisplayUpdateCtrl2);
    cs_release();
    if (!wait_busy(Panel::BUSY_TIMEOUT_CMD_MS))
        return false;

    powered_ = false;
    return true;
}

bool Gdey027t91::trigger_update(uint8_t sequence, uint32_t busy_timeout_ms)
{
    cmd(Cmd::kDisplayUpdateCtrl1);
    dat(sequence);
    cmd(Cmd::kDisplayUpdateCtrl2);
    cs_release();
    return wait_busy(busy_timeout_ms);
}

bool Gdey027t91::ensure_power()
{
    return power_on();
}

bool Gdey027t91::cold_boot()
{
    hw_reset();
    if (!sw_reset())
        return false;
    configure_panel();
    drop_session();
    return ensure_power();
}

bool Gdey027t91::present_base()
{
    if (!ensure_power())
        return false;

    set_border_waveform(Param::kBorderNormal);
    set_full_ram_entry();
    write_ram_full(Cmd::kWriteRamBw, framebuffer_);
    write_ram_full(Cmd::kWriteRamBwPrev, framebuffer_);

    set_fast_booster_timing();
    if (!trigger_update(Seq::kFast, Panel::BUSY_TIMEOUT_FAST_MS))
        return false;

    base_ok_ = true;
    return true;
}

bool Gdey027t91::present_full()
{
    if (!wait_busy(Panel::BUSY_TIMEOUT_CMD_MS))
        return false;
    if (!ensure_power())
        return false;

    set_border_waveform(Param::kBorderNormal);
    set_full_ram_entry();
    write_ram_full(Cmd::kWriteRamBw, framebuffer_);
    write_ram_full(Cmd::kWriteRamBwPrev, framebuffer_);

    if (!trigger_update(Seq::kFull, Panel::BUSY_TIMEOUT_FULL_MS))
        return false;

    (void)power_off();
    base_ok_ = true;
    return true;
}

bool Gdey027t91::present_fast()
{
    if (!base_ok_)
        return present_base();

    if (!wait_busy(Panel::BUSY_TIMEOUT_CMD_MS))
        return false;
    if (!ensure_power())
        return false;

    set_border_waveform(Param::kBorderNormal);
    set_full_ram_entry();
    write_ram_full(Cmd::kWriteRamBw, framebuffer_);

    set_fast_booster_timing();
    if (!trigger_update(Seq::kFast, Panel::BUSY_TIMEOUT_FAST_MS))
        return false;

    write_ram_full(Cmd::kWriteRamBwPrev, framebuffer_);

    base_ok_ = true;
    return true;
}

bool Gdey027t91::present_partial(const Window& win)
{
    if (!base_ok_)
        return false;

    if (!wait_busy(Panel::BUSY_TIMEOUT_CMD_MS))
        return false;
    if (!ensure_power())
        return false;

    set_border_waveform(Param::kBorderNormal);

    apply_window(win);

    const uint8_t* region_tl =
        framebuffer_ + (static_cast<uint32_t>(win.y) * STRIDE) + (win.x / 8U);

    write_ram_window(Cmd::kWriteRamBw, win, region_tl);
    if (!trigger_update(Seq::kPartial, Panel::BUSY_TIMEOUT_PARTIAL_MS))
        return false;

    apply_window(win);
    write_ram_window(Cmd::kWriteRamBwPrev, win, region_tl);

    set_full_ram_entry();
    cs_release();
    return true;
}

bool Gdey027t91::present(Present kind)
{
    if (!ensure_hw())
        return false;

    BusGuard lock(*this);
    if (!lock.ok())
        return false;

    switch (kind)
    {
        case Present::Base:
            return present_base();
        case Present::Fast:
            return present_fast();
        case Present::Full:
            return present_full();
        case Present::Partial:
            ESP_LOGE(TAG, "Partial 需要窗口参数");
            return false;
    }
    return false;
}

bool Gdey027t91::present(Present kind, Window win)
{
    if (!ensure_hw())
        return false;

    if (!win.normalize())
        return false;

    BusGuard lock(*this);
    if (!lock.ok())
        return false;

    if (kind != Present::Partial)
    {
        ESP_LOGE(TAG, "present(%d) 不接受窗口", static_cast<int>(kind));
        return false;
    }
    return present_partial(win);
}

void Gdey027t91::sleep()
{
    if (!initialized_)
        return;

    BusGuard lock(*this);
    if (!lock.ok())
        return;

    power_off();
    cmd(Cmd::kDeepSleep);
    dat(Param::kDeepSleepMode);
    cs_release();
    ::app::sys::task::TaskMgr::delay_ms(100);
    drop_session();
}

void Gdey027t91::wake()
{
    if (!initialized_)
        return;

    BusGuard lock(*this);
    if (!lock.ok())
        return;

    hw_reset();
    if (sw_reset())
        configure_panel();
    drop_session();
}

} // namespace app::bsp::driver::gdey027t91
