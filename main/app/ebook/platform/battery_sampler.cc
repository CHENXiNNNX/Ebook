#include "platform/battery_sampler.hpp"

#include "bsp/battery/battery.hpp"
#include "core/log.hpp"
#include "data/system_state.hpp"

static const char* const TAG = "BatterySampler";

namespace app::ebook::platform {

namespace {

::app::bsp::battery::Battery g_bat;

uint32_t ema(uint32_t prev, uint32_t raw, uint8_t k)
{
    if (k <= 1) return raw;
    return ((prev * (k - 1)) + raw) / k;
}

uint8_t mv_to_pct(uint32_t mv)
{
    return ::app::bsp::battery::Battery::mv_to_percent(mv);
}

} // namespace

BatterySampler& BatterySampler::get_instance()
{
    static BatterySampler s;
    return s;
}

bool BatterySampler::init()
{
    if (ready_) return true;
    if (!g_bat.init())
    {
        EBOOK_LOGW(TAG, "init failed");
        return false;
    }

    // 立即采一次作为 EMA / 迟滞基线
    const uint32_t mv = g_bat.read_voltage_mv();
    voltage_ema_mv_   = mv;
    ema_init_         = true;
    stable_pct_       = mv_to_pct(mv);
    pct_init_         = true;
    data::SystemState::get_instance().set_battery(stable_pct_, mv);
    ready_ = true;

    EBOOK_LOGI(TAG, "init OK: %u%% %umV",
               static_cast<unsigned>(stable_pct_),
               static_cast<unsigned>(mv));
    return true;
}

uint32_t BatterySampler::voltage_mv()
{
    return ready_ ? voltage_ema_mv_ : 0;
}

uint8_t BatterySampler::percent()
{
    if (!ready_) return data::SystemState::get_instance().battery_pct();

    const uint32_t raw_mv = g_bat.read_voltage_mv();

    voltage_ema_mv_ = ema_init_
        ? ema(voltage_ema_mv_, raw_mv, kEmaWeight)
        : raw_mv;
    ema_init_ = true;

    const uint8_t cand = mv_to_pct(voltage_ema_mv_);

    // 首次 / 显著下降 / 充电跳变
    const bool first       = !pct_init_;
    const bool drop_enough = (cand + kDropDeltaPct <= stable_pct_);
    const bool charge_jump = (cand >= stable_pct_ + kChargeJumpPct);

    if (first || drop_enough || charge_jump)
        stable_pct_ = cand;
    pct_init_ = true;

    data::SystemState::get_instance().set_battery(stable_pct_, voltage_ema_mv_);

    EBOOK_LOGD(TAG, "raw=%umV ema=%umV cand=%u stable=%u",
               static_cast<unsigned>(raw_mv),
               static_cast<unsigned>(voltage_ema_mv_),
               static_cast<unsigned>(cand),
               static_cast<unsigned>(stable_pct_));

    return stable_pct_;
}

} // namespace app::ebook::platform
