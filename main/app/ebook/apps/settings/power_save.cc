#include "apps/settings/power_save.hpp"

#include "apps/settings/settings_app.hpp"
#include "apps/settings/settings_internal.hpp"
#include "data/persist.hpp"
#include "data/system_state.hpp"
#include "network/wifi/wifi.hpp"
#include "overlays/confirm_prompt.hpp"
#include "router/refresh_intent.hpp"
#include "router/router.hpp"
#include "ui/strings.hpp"

namespace app::ebook::apps::settings {

namespace {

constexpr const char* kKOn   = "pwr.on";
constexpr const char* kKAuto = "pwr.auto";
constexpr const char* kKLvl  = "pwr.lvl";

using detail::ensure_wifi;

} // namespace

PowerSave& PowerSave::get_instance()
{
    static PowerSave s;
    return s;
}

void PowerSave::load()
{
    bool on = false;
    if (data::Persist::get_bool(kKOn, on) && on)
    {
        bool auto_on = false;
        (void)data::Persist::get_bool(kKAuto, auto_on);
        uint8_t lvl = 0;
        (void)data::Persist::get_u8(kKLvl, lvl);

        auto_enabled_ = auto_on;
        level_        = (lvl == static_cast<uint8_t>(Level::Strong)) ? Level::Strong
                                                                     : Level::Normal;
        capture_snapshot();
        enabled_ = true;
        apply_policy();
    }

    last_pct_ = data::SystemState::get_instance().battery_pct();
}

void PowerSave::capture_snapshot()
{
    if (snapshot_valid_)
        return;
    auto& sys          = data::SystemState::get_instance();
    snap_brightness_   = sys.brightness();
    snap_wifi_         = sys.wifi();
    snap_bt_           = sys.bluetooth();
    snap_hotspot_      = sys.hotspot();
    snapshot_valid_    = true;
}

void PowerSave::restore_snapshot()
{
    if (!snapshot_valid_)
        return;

    auto& sys = data::SystemState::get_instance();
    sys.set_brightness(snap_brightness_);
    sys.set_wifi(snap_wifi_);
    if (snap_wifi_)
    {
        if (!ensure_wifi())
            sys.set_wifi(false);
    }
    else
    {
        ::app::network::wifi::WiFiMgr::get_instance().disconnect();
    }

    sys.set_bluetooth(snap_bt_);
    SettingsApp::instance().bt_apply_state();

    sys.set_hotspot(snap_hotspot_);
    SettingsApp::instance().hotspot_apply_state();

    snapshot_valid_ = false;
}

void PowerSave::apply_policy()
{
    auto& sys = data::SystemState::get_instance();
    const uint8_t cap =
        (level_ == Level::Strong) ? kCapStrong : kCapNormal;
    if (sys.brightness() > cap)
        sys.set_brightness(cap);

    if (sys.wifi())
    {
        sys.set_wifi(false);
        ::app::network::wifi::WiFiMgr::get_instance().disconnect();
    }
    if (sys.bluetooth())
    {
        sys.set_bluetooth(false);
        SettingsApp::instance().bt_apply_state();
    }
    if (sys.hotspot())
    {
        sys.set_hotspot(false);
        SettingsApp::instance().hotspot_apply_state();
    }
}

void PowerSave::set_enabled(bool on, Level level, bool from_auto)
{
    if (on)
    {
        if (!enabled_)
            capture_snapshot();
        enabled_      = true;
        level_        = level;
        auto_enabled_ = from_auto;
        apply_policy();

        (void)data::Persist::set_bool(kKOn, true);
        (void)data::Persist::set_bool(kKAuto, auto_enabled_);
        (void)data::Persist::set_u8(kKLvl, static_cast<uint8_t>(level_));
        data::Persist::commit();
        (void)router::Router::instance().repaint(router::intent_partial_full());
        return;
    }

    if (!enabled_)
        return;

    enabled_      = false;
    auto_enabled_ = false;
    restore_snapshot();

    (void)data::Persist::set_bool(kKOn, false);
    (void)data::Persist::set_bool(kKAuto, false);
    data::Persist::commit();
    (void)router::Router::instance().repaint(router::intent_partial_full());
}

void PowerSave::toggle_manual()
{
    if (enabled_)
        set_enabled(false, level_, false);
    else
        set_enabled(true, Level::Normal, false);
}

void PowerSave::prompt_enable(Level level)
{
    if (overlays::ConfirmPrompt::instance().is_open())
        return;

    pending_level_ = level;
    const char* msg = (level == Level::Strong) ? ui::strings::kPwrPromptEnable10
                                               : ui::strings::kPwrPromptEnable20;
    overlays::ConfirmPrompt::instance().show(msg, on_enable_choice, this);
}

void PowerSave::prompt_disable()
{
    if (overlays::ConfirmPrompt::instance().is_open())
        return;

    overlays::ConfirmPrompt::instance().show(ui::strings::kPwrPromptDisable80,
                                             on_disable_choice, this);
}

void PowerSave::on_enable_choice(bool accepted, void* user)
{
    auto* self = static_cast<PowerSave*>(user);
    if (self == nullptr || !accepted)
        return;
    self->set_enabled(true, self->pending_level_, true);
}

void PowerSave::on_disable_choice(bool accepted, void* user)
{
    auto* self = static_cast<PowerSave*>(user);
    if (self == nullptr || !accepted)
        return;
    self->set_enabled(false, self->level_, false);
}

void PowerSave::on_battery_pct(uint8_t pct)
{
    const uint8_t prev = last_pct_;
    last_pct_          = pct;

    if (prev == 0xFF)
        return;
    if (overlays::ConfirmPrompt::instance().is_open())
        return;

    if (prev > kThreshLow10 && pct <= kThreshLow10)
    {
        if (!enabled_)
            prompt_enable(Level::Strong);
        else if (level_ == Level::Normal)
            prompt_enable(Level::Strong);
        return;
    }

    if (prev > kThreshLow20 && pct <= kThreshLow20)
    {
        if (!enabled_)
            prompt_enable(Level::Normal);
        return;
    }

    if (prev <= kThreshHigh80 && pct > kThreshHigh80)
    {
        if (enabled_ && auto_enabled_)
            prompt_disable();
    }
}

} // namespace app::ebook::apps::settings
