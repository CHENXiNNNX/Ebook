#pragma once

#include <cstdint>

namespace app::ebook::platform {

/**
 * @brief 电池采样（EMA K=5；下降 ≥2% / 充电上升 ≥5% 才更新；写 SystemState）
 */
class BatterySampler
{
  public:
    static BatterySampler& get_instance();

    bool init();
    bool ready() const { return ready_; }

    uint8_t  percent();
    uint32_t voltage_mv();

  private:
    BatterySampler() = default;
    ~BatterySampler() = default;

    static constexpr uint8_t kEmaWeight     = 5;
    static constexpr uint8_t kDropDeltaPct  = 2;
    static constexpr uint8_t kChargeJumpPct = 5;

    bool     ready_{false};
    bool     ema_init_{false};
    bool     pct_init_{false};
    uint32_t voltage_ema_mv_{0};
    uint8_t  stable_pct_{0};
};

} // namespace app::ebook::platform
