#include "button.hpp"

#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_log.h>

#include "config/config.hpp"

static const char* const TAG = "DipSwitch";

namespace app::bsp::driver::button {

namespace {

constexpr adc_atten_t kAdcAtten = ADC_ATTEN_DB_12;
constexpr adc_bitwidth_t kAdcWidth = ADC_BITWIDTH_12;

DipId classify_position(uint32_t mv, const Config& cfg)
{
    if (mv >= cfg.threshold_released_mv)
    {
        return DipId::NONE;
    }
    if (mv < cfg.threshold_up_mid_mv)
    {
        return DipId::UP;
    }
    if (mv < cfg.threshold_mid_down_mv)
    {
        return DipId::MID;
    }
    return DipId::DOWN;
}

DipState state_of(DipId pos)
{
    DipState st = {};
    switch (pos)
    {
        case DipId::UP:
            st.up = true;
            break;
        case DipId::MID:
            st.mid = true;
            break;
        case DipId::DOWN:
            st.down = true;
            break;
        case DipId::NONE:
            break;
    }
    return st;
}

uint8_t mask_of(DipId pos)
{
    switch (pos)
    {
        case DipId::UP:
            return 1U << 0;
        case DipId::MID:
            return 1U << 1;
        case DipId::DOWN:
            return 1U << 2;
        case DipId::NONE:
            break;
    }
    return 0;
}

} // namespace

DipSwitch::DipSwitch()
    : cfg_{}
    , adc_handle_(nullptr)
    , cali_handle_(nullptr)
    , adc_unit_(-1)
    , adc_channel_(-1)
    , initialized_(false)
    , cali_ok_(false)
{
}

DipSwitch::~DipSwitch()
{
    deinit();
}

bool DipSwitch::init(const Config* config)
{
    if (initialized_)
    {
        return true;
    }

    cfg_ = (config != nullptr) ? *config : Config();

    adc_unit_t unit = ADC_UNIT_1;
    adc_channel_t channel = ADC_CHANNEL_0;
    if (adc_oneshot_io_to_channel(config::DIP_SWITCH, &unit, &channel) != ESP_OK)
    {
        ESP_LOGE(TAG, "GPIO%d 无法映射 ADC 通道", static_cast<int>(config::DIP_SWITCH));
        return false;
    }
    adc_unit_ = static_cast<int>(unit);
    adc_channel_ = static_cast<int>(channel);

    adc_oneshot_unit_init_cfg_t unit_cfg = {};
    unit_cfg.unit_id = unit;
    unit_cfg.ulp_mode = ADC_ULP_MODE_DISABLE;

    adc_oneshot_unit_handle_t handle = nullptr;
    if (adc_oneshot_new_unit(&unit_cfg, &handle) != ESP_OK)
    {
        ESP_LOGE(TAG, "ADC unit 初始化失败");
        return false;
    }
    adc_handle_ = handle;

    adc_oneshot_chan_cfg_t chan_cfg = {};
    chan_cfg.atten = kAdcAtten;
    chan_cfg.bitwidth = kAdcWidth;

    if (adc_oneshot_config_channel(handle, channel, &chan_cfg) != ESP_OK)
    {
        ESP_LOGE(TAG, "ADC 通道配置失败");
        deinit();
        return false;
    }

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_cfg = {};
    cali_cfg.unit_id = unit;
    cali_cfg.chan = channel;
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
    cali_cfg.unit_id = unit;
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

    const uint32_t mv = read_voltage_mv();
    const DipState st = read();
    ESP_LOGI(TAG, "OK  GPIO%d  %umV  up=%d  mid=%d  down=%d  mask=0x%02X",
             static_cast<int>(config::DIP_SWITCH), static_cast<unsigned>(mv), st.up ? 1 : 0,
             st.mid ? 1 : 0, st.down ? 1 : 0, read_mask());
    return true;
}

void DipSwitch::deinit()
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

    adc_unit_ = -1;
    adc_channel_ = -1;
    initialized_ = false;
    cali_ok_ = false;
}

uint32_t DipSwitch::read_voltage_mv() const
{
    if (!initialized_ || adc_handle_ == nullptr || adc_channel_ < 0)
    {
        return 0;
    }

    auto* handle = static_cast<adc_oneshot_unit_handle_t>(adc_handle_);
    const auto channel = static_cast<adc_channel_t>(adc_channel_);
    const uint32_t samples = (cfg_.sample_count > 0) ? cfg_.sample_count : 1;

    int32_t sum = 0;
    uint32_t ok_count = 0;
    for (uint32_t i = 0; i < samples; i++)
    {
        int raw = 0;
        if (adc_oneshot_read(handle, channel, &raw) == ESP_OK)
        {
            sum += raw;
            ok_count++;
        }
    }
    if (ok_count == 0)
    {
        return 0;
    }

    const int avg_raw = static_cast<int>(sum / static_cast<int32_t>(ok_count));

    int voltage_mv = 0;
    if (cali_ok_)
    {
        adc_cali_raw_to_voltage(static_cast<adc_cali_handle_t>(cali_handle_), avg_raw, &voltage_mv);
    }
    else
    {
        voltage_mv = static_cast<int>((static_cast<uint32_t>(avg_raw) * 3100) / 4095);
    }

    return static_cast<uint32_t>(voltage_mv);
}

DipId DipSwitch::read_position() const
{
    return classify_position(read_voltage_mv(), cfg_);
}

DipState DipSwitch::read() const
{
    if (!initialized_)
    {
        return {};
    }
    return state_of(read_position());
}

bool DipSwitch::read(DipId id) const
{
    if (!initialized_)
    {
        return false;
    }
    return read_position() == id;
}

uint8_t DipSwitch::read_mask() const
{
    if (!initialized_)
    {
        return 0;
    }
    return mask_of(read_position());
}

} // namespace app::bsp::driver::button
