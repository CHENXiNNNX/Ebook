#include "platform/boot.hpp"

#include <cstdio>

#include <esp_app_desc.h>
#include <esp_mac.h>
#include <nvs_flash.h>

#include "apps/app_registry.hpp"
#include "apps/settings/auto_lock.hpp"
#include "apps/settings/power_save.hpp"
#include "apps/settings/lock_password.hpp"
#include "apps/settings/settings_app.hpp"
#include "config/config.hpp"
#include "core/log.hpp"
#include "data/clock_provider.hpp"
#include "apps/clock/clock_store.hpp"
#include "apps/music/music_store.hpp"
#include "apps/reader/reading_store.hpp"
#include "data/persist.hpp"
#include "data/system_state.hpp"
#include "display/display_port.hpp"
#include "ft6336u.hpp"
#include "gfx/font.hpp"
#include "i2c/i2c.hpp"
#include "input/input_router.hpp"
#include "input/key_bindings.hpp"
#include "input/physical_input.hpp"
#include "ota/ota.hpp"
#include "platform/audio_codec.hpp"
#include "platform/battery_sampler.hpp"
#include "platform/housekeeper.hpp"
#include "platform/music_player.hpp"
#include "platform/wooden_fish_sound.hpp"
#include "presenter/presenter.hpp"
#include "network/wifi/wifi.hpp"
#include "protocol/http/http.hpp"
#include "protocol/ntp/ntp.hpp"
#include "storage/storage.hpp"
#include "system/event/event.hpp"
#include "ui/ui_bus.hpp"
#include "ui/ui_loop.hpp"

static const char* const TAG = "Boot";

namespace app::ebook::platform {

namespace {

::app::bsp::i2c::I2C g_i2c;
::app::bsp::driver::ft6336u::Ft6336u g_touch;

using StepFn = core::Status (*)();

struct BootStep
{
    const char* name;
    StepFn run;
    bool required;
};

bool init_nvs_partition()
{
    esp_err_t r = nvs_flash_init();
    if (r == ESP_ERR_NVS_NO_FREE_PAGES || r == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        r = nvs_flash_init();
    }
    return r == ESP_OK;
}

core::Status step_system_services()
{
    if (!init_nvs_partition()) EBOOK_LOGW(TAG, "nvs partial");
    if (!::app::sys::event::EventMgr::get_instance().init()) EBOOK_LOGW(TAG, "EventMgr");
    if (!::app::protocol::http::HttpMgr::get_instance().init()) EBOOK_LOGW(TAG, "HttpMgr");
    if (!::app::protocol::ntp::NtpMgr::get_instance().init()) EBOOK_LOGW(TAG, "NtpMgr");

    ::app::protocol::ntp::NtpMgr::get_instance().set_sync_callback(
        [](::app::protocol::ntp::SyncStatus st) {
            (void)::app::ebook::ui::UiBus::get_instance().post_ntp_sync_done(
                static_cast<uint8_t>(st));
        });

    char device_id[18] = {};
    uint8_t mac[6] = {};
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK)
    {
        (void)std::snprintf(device_id, sizeof(device_id),
                            "%02x%02x%02x%02x%02x%02x",
                            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
    const esp_app_desc_t* desc = esp_app_get_description();
    const char* ver = (desc != nullptr) ? desc->version : "0.0.0";
    if (!::app::common::ota::OtaMgr::get_instance().init(device_id, ver))
        EBOOK_LOGW(TAG, "OtaMgr");

    if (::app::common::storage::StorageMgr::get_instance().init_defaults() != ESP_OK)
        EBOOK_LOGW(TAG, "storage defaults");

    return core::Status::Ok;
}

core::Status step_persist()
{
    return data::Persist::init();
}

core::Status step_display()
{
    if (!display::DisplayPort::instance().init())
        return core::Status::IoError;
    if (!gfx::Font::get_instance().init())
        EBOOK_LOGW(TAG, "font partial");
    return core::Status::Ok;
}

core::Status step_touch()
{
    if (!g_i2c.init()) return core::Status::IoError;

    ::app::bsp::driver::ft6336u::Config tc{};
    tc.panel_width  = core::kScreenW;
    tc.panel_height = core::kScreenH;
    tc.swap_xy      = ::app::config::TOUCH_SWAP_XY;
    tc.mirror_x     = ::app::config::TOUCH_MIRROR_X;
    tc.mirror_y     = ::app::config::TOUCH_MIRROR_Y;

    if (!g_touch.init(g_i2c.get_bus_handle(), &tc))
        return core::Status::IoError;
    (void)g_touch.enable_interrupt();

    if (!input::InputRouter::get_instance().init(&g_touch))
        return core::Status::IoError;
    return core::Status::Ok;
}

core::Status step_audio()
{
    if (!AudioCodec::get_instance().init(g_i2c.get_bus_handle()))
    {
        EBOOK_LOGW(TAG, "audio init fail");
        return core::Status::Ok;
    }
    if (!MusicPlayer::get_instance().init())
        EBOOK_LOGW(TAG, "music player init fail");
    if (!WoodenFishSound::get_instance().init())
        EBOOK_LOGW(TAG, "wooden fish sound init fail");
    return core::Status::Ok;
}

void wire_system_state_observers()
{
    data::SystemState::get_instance().set_brightness_observer(
        [](uint8_t v) { display::DisplayPort::instance().set_brightness(v); });
    data::SystemState::get_instance().set_volume_observer(
        [](uint8_t /*v*/) { AudioCodec::get_instance().apply_system_volume(); });
}

core::Status step_data_load()
{
    data::SystemState::get_instance().load();
    apps::settings::LockPassword::get_instance().load();
    apps::settings::AutoLock::get_instance().load();
    apps::settings::PowerSave::get_instance().load();
    input::KeyBindings::instance().load();
    apps::reader::ReadingStore::get_instance().load();
    apps::clock::ClockStore::get_instance().load();
    apps::music::MusicStore::get_instance().load();
    return core::Status::Ok;
}

core::Status step_battery()
{
    BatterySampler::get_instance().init();
    return core::Status::Ok;
}

core::Status step_dip_switch()
{
    if (!input::PhysicalInput::get_instance().init())
        EBOOK_LOGW(TAG, "dip switch unavailable");
    return core::Status::Ok;
}

core::Status step_tasks_start()
{
    if (!presenter::Presenter::instance().init())
        return core::Status::OutOfResource;
    if (!ui::UiBus::get_instance().init())
        return core::Status::OutOfResource;

    apps::AppRegistry::instance().register_defaults();

    if (!presenter::Presenter::instance().start())
        return core::Status::IoError;
    if (!ui::UiLoop::instance().init())
        return core::Status::Internal;
    if (!ui::UiLoop::instance().start())
        return core::Status::Internal;

    {
        const auto clk = data::Clock::now();
        (void)ui::UiBus::get_instance().post_tick_clock(clk.hour, clk.minute);
    }

    if (BatterySampler::get_instance().ready())
    {
        const auto& st = data::SystemState::get_instance();
        (void)ui::UiBus::get_instance().post_tick_battery(st.battery_pct(), st.battery_mv());
    }

    if (!input::InputRouter::get_instance().start())
        return core::Status::Internal;

    if (input::PhysicalInput::get_instance().is_ready())
    {
        if (!input::PhysicalInput::get_instance().start())
            EBOOK_LOGW(TAG, "dip input task start failed");
    }

    display::DisplayPort::instance().set_brightness(
        data::SystemState::get_instance().brightness());
    AudioCodec::get_instance().apply_system_volume();

    apps::settings::SettingsApp::instance().hotspot_apply_state();
    apps::settings::SettingsApp::instance().bt_apply_state();

    ::app::network::wifi::WiFiMgr::get_instance().set_state_cb(
        [](::app::network::wifi::State st, ::app::network::wifi::FailureReason r) {
            (void)ui::UiBus::get_instance().post_wifi_state(static_cast<uint8_t>(st),
                                                              static_cast<uint8_t>(r));
        });

    Housekeeper::get_instance().start();
    return core::Status::Ok;
}

constexpr BootStep kSteps[] = {
    {"system", step_system_services, false},
    {"persist", step_persist, false},
    {"display", step_display, true},
    {"touch", step_touch, true},
    {"audio", step_audio, false},
    {"data", step_data_load, false},
    {"battery", step_battery, false},
    {"dip", step_dip_switch, false},
    {"tasks", step_tasks_start, true},
};

} // namespace

core::Status boot()
{
    wire_system_state_observers();

    for (const BootStep& step : kSteps)
    {
        const core::Status st = step.run();
        if (st != core::Status::Ok)
        {
            EBOOK_LOGE(TAG, "step %s failed: %s", step.name, core::to_str(st));
            if (step.required)
                return st;
        }
    }

    EBOOK_LOGI(TAG, "boot ok");
    return core::Status::Ok;
}

} // namespace app::ebook::platform
