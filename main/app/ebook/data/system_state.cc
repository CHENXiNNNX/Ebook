#include "data/system_state.hpp"

#include "data/persist.hpp"

namespace app::ebook::data {

namespace {

// NVS key
constexpr const char* kKWifi       = "wifi";
constexpr const char* kKBt         = "bt";
constexpr const char* kKHotspot    = "hotspot";
constexpr const char* kKNight      = "night";
constexpr const char* kKMute       = "mute";
constexpr const char* kKBrightness = "bri";
constexpr const char* kKVolume     = "vol";

void update_bool(bool& field, bool v, const char* key)
{
    if (v == field)
        return;
    field = v;
    Persist::set_bool(key, v);
    Persist::commit();
}

} // namespace

SystemState& SystemState::get_instance()
{
    static SystemState s;
    return s;
}

void SystemState::load()
{
    bool b = false;
    if (Persist::get_bool(kKWifi,    b)) wifi_       = b;
    if (Persist::get_bool(kKBt,      b)) bluetooth_  = b;
    if (Persist::get_bool(kKHotspot, b)) hotspot_    = b;
    if (Persist::get_bool(kKNight,   b)) night_mode_ = b;
    if (Persist::get_bool(kKMute,    b)) mute_       = b;

    uint8_t u = 0;
    if (Persist::get_u8(kKBrightness, u)) brightness_ = u;
    if (Persist::get_u8(kKVolume,     u)) volume_     = u;
}

void SystemState::save()
{
    Persist::set_bool(kKWifi,    wifi_);
    Persist::set_bool(kKBt,      bluetooth_);
    Persist::set_bool(kKHotspot, hotspot_);
    Persist::set_bool(kKNight,   night_mode_);
    Persist::set_bool(kKMute,    mute_);
    Persist::set_u8  (kKBrightness, brightness_);
    Persist::set_u8  (kKVolume,     volume_);
    Persist::commit();
}

void SystemState::set_wifi(bool v)       { update_bool(wifi_,       v, kKWifi); }
void SystemState::set_bluetooth(bool v)  { update_bool(bluetooth_,  v, kKBt); }
void SystemState::set_hotspot(bool v)    { update_bool(hotspot_,    v, kKHotspot); }

void SystemState::set_night_mode(bool v)
{
    if (v == night_mode_)
        return;
    update_bool(night_mode_, v, kKNight);
    if (night_cb_)
        night_cb_(v);
}

void SystemState::set_mute(bool v)
{
    update_bool(mute_, v, kKMute);
    if (volume_cb_)
        volume_cb_(volume_);
}

void SystemState::set_brightness(uint8_t v)
{
    if (v == brightness_)
        return;
    brightness_ = v;
    Persist::set_u8(kKBrightness, v);
    Persist::commit();
    if (bright_cb_)
        bright_cb_(v);
}

void SystemState::set_volume(uint8_t v)
{
    if (v == volume_)
        return;
    volume_ = v;
    Persist::set_u8(kKVolume, v);
    Persist::commit();
    if (volume_cb_)
        volume_cb_(v);
}

void SystemState::set_battery(uint8_t pct, uint32_t mv)
{
    battery_pct_ = pct;
    battery_mv_  = mv;
}

} // namespace app::ebook::data
