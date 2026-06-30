#pragma once

#include <driver/gpio.h>
#include <driver/i2c_master.h>

namespace app::config {
// i2c pins
const gpio_num_t I2C_SDA = GPIO_NUM_2;
const gpio_num_t I2C_SCL = GPIO_NUM_1;

// es8311 (i2s)
const gpio_num_t I2S_MCLK = GPIO_NUM_41;
const gpio_num_t I2S_SCLK = GPIO_NUM_40;
const gpio_num_t I2S_LRCK = GPIO_NUM_39;
const gpio_num_t I2S_DI = GPIO_NUM_42;
const gpio_num_t I2S_DO = GPIO_NUM_38;
const gpio_num_t I2S_PA = GPIO_NUM_7;

// battery
const gpio_num_t BATTERY_VBAT = GPIO_NUM_6;
// ADC 两点标定：满电时实测约 4170mV
constexpr uint32_t BATTERY_FULL_MV = 4170;
constexpr uint32_t BATTERY_EMPTY_MV = 3000;

// gdey027t91 pins
const gpio_num_t GDEY027T91_MOSI = GPIO_NUM_3;
const gpio_num_t GDEY027T91_MISO = GPIO_NUM_NC; // not used
const gpio_num_t GDEY027T91_SCLK = GPIO_NUM_46;
const gpio_num_t GDEY027T91_CS = GPIO_NUM_9;
const gpio_num_t GDEY027T91_DC = GPIO_NUM_10;
const gpio_num_t GDEY027T91_RST = GPIO_NUM_11;
const gpio_num_t GDEY027T91_BUSY = GPIO_NUM_12;
const gpio_num_t GDEY027T91_Backlight = GPIO_NUM_17; // 背光，通过pwm驱动

// FT6336 
const gpio_num_t FT6336_RST = GPIO_NUM_18;
const gpio_num_t FT6336_INT = GPIO_NUM_5;
constexpr bool TOUCH_SWAP_XY = false;
constexpr bool TOUCH_MIRROR_X = false; 
constexpr bool TOUCH_MIRROR_Y = false;

// tf-card pins
const gpio_num_t TF_CARD_D0   = GPIO_NUM_14;
const gpio_num_t TF_CARD_D1   = GPIO_NUM_13;
const gpio_num_t TF_CARD_D2   = GPIO_NUM_45;
const gpio_num_t TF_CARD_D3   = GPIO_NUM_48;
const gpio_num_t TF_CARD_CLK  = GPIO_NUM_21;
const gpio_num_t TF_CARD_CMD  = GPIO_NUM_47;
const gpio_num_t TF_CARD_CD   = GPIO_NUM_4;

// ADC 拨码 — GPIO15；实测 up=551mV mid=1102mV down=1651mV 未按=3300mV
const gpio_num_t DIP_SWITCH = GPIO_NUM_15;
constexpr uint32_t DIP_SWITCH_THRESHOLD_UP_MID_MV = 830;      // (551 + 1102) / 2
constexpr uint32_t DIP_SWITCH_THRESHOLD_MID_DOWN_MV = 1380;   // (1102 + 1651) / 2
constexpr uint32_t DIP_SWITCH_THRESHOLD_RELEASED_MV = 2100;   // 高于 down，低于未按

} // namespace app::config
