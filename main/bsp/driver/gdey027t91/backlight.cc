#include "backlight.hpp"

#include <algorithm>

#include <driver/ledc.h>
#include <esp_log.h>

#include "config/config.hpp"

static const char* const TAG = "Backlight";

namespace app::bsp::driver::gdey027t91 {

namespace {

constexpr ledc_mode_t kMode = LEDC_LOW_SPEED_MODE;
constexpr ledc_timer_t kTimer = LEDC_TIMER_0;
constexpr ledc_channel_t kChannel = LEDC_CHANNEL_0;
constexpr uint32_t kFreqHz = 50'000;
constexpr ledc_timer_bit_t kBits = LEDC_TIMER_8_BIT;
constexpr uint32_t kMaxDuty = (1U << 8) - 1U;

bool g_timer_ready = false;

} // namespace

Backlight::Backlight() = default;

Backlight::~Backlight()
{
    deinit();
}

bool Backlight::init()
{
    if (ready_)
        return true;

    if (!g_timer_ready)
    {
        ledc_timer_config_t tcfg = {};
        tcfg.speed_mode = kMode;
        tcfg.duty_resolution = kBits;
        tcfg.timer_num = kTimer;
        tcfg.freq_hz = kFreqHz;
        tcfg.clk_cfg = LEDC_AUTO_CLK;
        if (ledc_timer_config(&tcfg) != ESP_OK)
        {
            ESP_LOGE(TAG, "timer 配置失败");
            return false;
        }
        g_timer_ready = true;
    }

    ledc_channel_config_t ccfg = {};
    ccfg.gpio_num = config::GDEY027T91_Backlight;
    ccfg.speed_mode = kMode;
    ccfg.channel = kChannel;
    ccfg.timer_sel = kTimer;
    ccfg.duty = 0;
    if (ledc_channel_config(&ccfg) != ESP_OK)
    {
        ESP_LOGE(TAG, "channel 配置失败");
        return false;
    }

    ready_ = true;
    apply_duty();
    ESP_LOGI(TAG, "OK GPIO=%d %u%%", config::GDEY027T91_Backlight, brightness_);
    return true;
}

void Backlight::deinit()
{
    if (!ready_)
        return;
    ledc_stop(kMode, kChannel, 0);
    ready_ = false;
}

void Backlight::apply_duty()
{
    if (!ready_)
        return;
    const uint32_t duty = (static_cast<uint32_t>(brightness_) * kMaxDuty) / 100U;
    ledc_set_duty(kMode, kChannel, duty);
    ledc_update_duty(kMode, kChannel);
}

void Backlight::set_brightness(uint8_t percent)
{
    brightness_ = std::min<uint8_t>(percent, 100);
    apply_duty();
}

void Backlight::on()
{
    brightness_ = (saved_ != 0) ? saved_ : 80;
    apply_duty();
}

void Backlight::off()
{
    saved_ = brightness_;
    brightness_ = 0;
    apply_duty();
}

} // namespace app::bsp::driver::gdey027t91
