#pragma once

#include <cstdint>

namespace app::bsp::battery {

/**
 * @brief 锂电池 ADC 采样（TP4057 + 分压）
 */
class Battery
{
  public:
    Battery();
    ~Battery();

    bool init();
    void deinit();

    /** 读取电池电压 (mV)。内部 ADC 多次平均一次。 */
    uint32_t read_voltage_mv();

    /** 读取电量百分比 (0~100)。等价于 mv_to_percent(read_voltage_mv()) */
    uint8_t read_percent();

    /**
     * @brief 纯电压→百分比转换（无 ADC IO，可用于已平滑过的电压）
     *
     * 上层在做 EMA / 迟滞滤波时复用本函数避免重复读 ADC。
     */
    static uint8_t mv_to_percent(uint32_t mv);

    bool is_init() const
    {
        return initialized_;
    }

  private:
    static constexpr uint32_t kSampleCount = 32;  // ADC 单次读取的内部平均次数
    static constexpr uint32_t kDividerFactor = 2; // 100k/100k 分压比

    void* adc_handle_;
    void* cali_handle_;
    bool initialized_;
    bool cali_ok_;
};

} // namespace app::bsp::battery
