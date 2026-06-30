#include "battery.hpp"

#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_log.h>

#include "config/config.hpp"

static const char* const TAG = "Battery";

namespace app::bsp::battery {

namespace {
constexpr adc_channel_t kAdcChannel = ADC_CHANNEL_5; // GPIO6
constexpr adc_unit_t kAdcUnit = ADC_UNIT_1;
constexpr adc_atten_t kAdcAtten = ADC_ATTEN_DB_12;
constexpr adc_bitwidth_t kAdcWidth = ADC_BITWIDTH_12;

// 锂电池放电曲线（非线性），电压 (mV) → 电量 (%)
struct VoltPct
{
    uint32_t mv;
    uint8_t pct;
};

constexpr uint32_t kNominalFullMv = 4200;
constexpr uint32_t kNominalEmptyMv = 3000;

constexpr VoltPct kDischargeCurve[] = {
    {4200, 100}, {4150, 95}, {4110, 90}, {4080, 85}, {4020, 80}, {3980, 70}, {3950, 60}, {3910, 50},
    {3870, 40},  {3830, 30}, {3800, 20}, {3750, 15}, {3700, 10}, {3600, 5},  {3300, 1},  {3000, 0},
};
constexpr size_t kCurveLen = sizeof(kDischargeCurve) / sizeof(kDischargeCurve[0]);

uint8_t nominal_voltage_to_percent(uint32_t mv)
{
    if (mv >= kDischargeCurve[0].mv)
        return 100;
    if (mv <= kDischargeCurve[kCurveLen - 1].mv)
        return 0;

    for (size_t i = 0; i < kCurveLen - 1; i++)
    {
        const auto& hi = kDischargeCurve[i];
        const auto& lo = kDischargeCurve[i + 1];
        if (mv >= lo.mv)
        {
            uint32_t pct = lo.pct + (((mv - lo.mv) * (hi.pct - lo.pct)) / (hi.mv - lo.mv));
            return static_cast<uint8_t>(pct);
        }
    }
    return 0;
}

uint32_t calibrate_voltage_to_nominal(uint32_t mv)
{
    const uint32_t full_mv = config::BATTERY_FULL_MV;
    const uint32_t empty_mv = config::BATTERY_EMPTY_MV;
    if (mv >= full_mv)
        return kNominalFullMv;
    if (mv <= empty_mv)
        return kNominalEmptyMv;
    const uint32_t span = full_mv - empty_mv;
    if (span == 0)
        return kNominalEmptyMv;
    return empty_mv + ((mv - empty_mv) * (kNominalFullMv - kNominalEmptyMv)) / span;
}

uint8_t voltage_to_percent(uint32_t mv)
{
    if (mv >= config::BATTERY_FULL_MV)
        return 100;
    if (mv <= config::BATTERY_EMPTY_MV)
        return 0;
    return nominal_voltage_to_percent(calibrate_voltage_to_nominal(mv));
}
} // namespace

Battery::Battery()
    : adc_handle_(nullptr)
    , cali_handle_(nullptr)
    , initialized_(false)
    , cali_ok_(false)
{
}

Battery::~Battery()
{
    deinit();
}

bool Battery::init()
{
    if (initialized_)
        return true;

    // ADC oneshot 初始化
    adc_oneshot_unit_init_cfg_t unit_cfg = {};
    unit_cfg.unit_id = kAdcUnit;
    unit_cfg.ulp_mode = ADC_ULP_MODE_DISABLE;

    adc_oneshot_unit_handle_t handle = nullptr;
    if (adc_oneshot_new_unit(&unit_cfg, &handle) != ESP_OK)
    {
        ESP_LOGE(TAG, "ADC unit 初始化失败");
        return false;
    }
    adc_handle_ = handle;

    // 通道配置
    adc_oneshot_chan_cfg_t chan_cfg = {};
    chan_cfg.atten = kAdcAtten;
    chan_cfg.bitwidth = kAdcWidth;

    if (adc_oneshot_config_channel(handle, kAdcChannel, &chan_cfg) != ESP_OK)
    {
        ESP_LOGE(TAG, "ADC 通道配置失败");
        deinit();
        return false;
    }

    // 校准（优先 curve fitting，回退 line fitting）
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_cfg = {};
    cali_cfg.unit_id = kAdcUnit;
    cali_cfg.chan = kAdcChannel;
    cali_cfg.atten = kAdcAtten;
    cali_cfg.bitwidth = kAdcWidth;

    adc_cali_handle_t cali = nullptr;
    if (adc_cali_create_scheme_curve_fitting(&cali_cfg, &cali) == ESP_OK)
    {
        cali_handle_ = cali;
        cali_ok_ = true;
    }
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t cali_cfg = {};
    cali_cfg.unit_id = kAdcUnit;
    cali_cfg.atten = kAdcAtten;
    cali_cfg.bitwidth = kAdcWidth;

    adc_cali_handle_t cali = nullptr;
    if (adc_cali_create_scheme_line_fitting(&cali_cfg, &cali) == ESP_OK)
    {
        cali_handle_ = cali;
        cali_ok_ = true;
    }
#endif

    if (!cali_ok_)
    {
        ESP_LOGW(TAG, "ADC 校准不可用，电压读数可能不准确");
    }

    initialized_ = true;
    ESP_LOGI(TAG, "OK  GPIO%d  cali=%s", static_cast<int>(config::BATTERY_VBAT),
             cali_ok_ ? "yes" : "no");
    return true;
}

void Battery::deinit()
{
    if (cali_handle_ != nullptr)
    {
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
        adc_cali_delete_scheme_curve_fitting(static_cast<adc_cali_handle_t>(cali_handle_));
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
        adc_cali_delete_scheme_line_fitting(static_cast<adc_cali_handle_t>(cali_handle_));
#endif
        cali_handle_ = nullptr;
    }
    if (adc_handle_ != nullptr)
    {
        adc_oneshot_del_unit(static_cast<adc_oneshot_unit_handle_t>(adc_handle_));
        adc_handle_ = nullptr;
    }
    initialized_ = false;
    cali_ok_ = false;
}

uint32_t Battery::read_voltage_mv()
{
    if (!initialized_)
        return 0;

    auto* handle = static_cast<adc_oneshot_unit_handle_t>(adc_handle_);

    // 多次采样取平均
    int32_t sum = 0;
    for (uint32_t i = 0; i < kSampleCount; i++)
    {
        int raw = 0;
        if (adc_oneshot_read(handle, kAdcChannel, &raw) == ESP_OK)
        {
            sum += raw;
        }
    }
    int avg_raw = static_cast<int>(sum / static_cast<int32_t>(kSampleCount));

    // 校准转换为电压
    int voltage_mv = 0;
    if (cali_ok_)
    {
        adc_cali_raw_to_voltage(static_cast<adc_cali_handle_t>(cali_handle_), avg_raw, &voltage_mv);
    }
    else
    {
        // 无校准时粗略估算：12-bit @ 12dB ≈ 0~3100mV
        voltage_mv = static_cast<int>((static_cast<uint32_t>(avg_raw) * 3100) / 4095);
    }

    // 乘以分压系数还原电池实际电压
    return static_cast<uint32_t>(voltage_mv) * kDividerFactor;
}

uint8_t Battery::read_percent()
{
    return voltage_to_percent(read_voltage_mv());
}

uint8_t Battery::mv_to_percent(uint32_t mv)
{
    return voltage_to_percent(mv);
}

} // namespace app::bsp::battery
