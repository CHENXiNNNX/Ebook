#pragma once

#include <cstdint>

#include "config/config.hpp"

namespace app::bsp::driver::button {

/** 拨码位置（NONE = 未按下） */
enum class DipId : uint8_t
{
    UP = 0,
    MID = 1,
    DOWN = 2,
    NONE = 3,
};

constexpr uint8_t kDipCount = 3;

/**
 * @brief 三路 DIP 拨码状态（ADC 三档互斥；未按下时全为 false）
 */
struct DipState
{
    bool up;
    bool mid;
    bool down;
};

/**
 * @brief ADC 拨码开关驱动
 *
 * 单 ADC 引脚（config::DIP_SWITCH）通过分压电阻区分三档按键；
 * 未按下时引脚被拉高至约 3.3V。
 */
struct Config
{
    uint32_t threshold_up_mid_mv = config::DIP_SWITCH_THRESHOLD_UP_MID_MV;
    uint32_t threshold_mid_down_mv = config::DIP_SWITCH_THRESHOLD_MID_DOWN_MV;
    uint32_t threshold_released_mv = config::DIP_SWITCH_THRESHOLD_RELEASED_MV;
    uint32_t sample_count = 8;
};

class DipSwitch
{
  public:
    DipSwitch();
    ~DipSwitch();

    bool init(const Config* config = nullptr);
    void deinit();

    /** 读取当前档位 */
    DipState read() const;

    /** 当前是否处于指定档位 */
    bool read(DipId id) const;

    /**
     * @brief 读取 3-bit 掩码
     * @return bit0=up, bit1=mid, bit2=down；位为 1 表示当前档位
     */
    uint8_t read_mask() const;

    /** 读取 ADC 电压 (mV)，便于标定阈值 */
    uint32_t read_voltage_mv() const;

    bool is_init() const
    {
        return initialized_;
    }

  private:
    DipId read_position() const;

    Config cfg_;
    void* adc_handle_;
    void* cali_handle_;
    int adc_unit_;
    int adc_channel_;
    bool initialized_;
    bool cali_ok_;
};

} // namespace app::bsp::driver::button
