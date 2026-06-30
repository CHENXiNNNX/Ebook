#pragma once

#include <cstdint>

#include "apps/app.hpp"
#include "input/input_bindings.hpp"
#include "ui/list_view.hpp"
#include "ui/ui_event.hpp"

namespace app::ebook::apps::settings {

enum class SettingsPage : uint8_t
{
    Menu = 0,
    Wifi,
    Bluetooth,
    Hotspot,
    Display,
    Keys,
    KeysEdit,
    Battery,
    Sound,
    Time,
    Storage,
    Security,
    About,
};

enum class SecPinPurpose : uint8_t
{
    None = 0,
    SetNew,
    ConfirmNew,
    VerifyForChange,
    VerifyForClear,
};

class SettingsApp : public App
{
  public:
    static SettingsApp& instance();

    AppId       id()      const override { return AppId::Settings; }
    const char* title()   const override;
    uint32_t    icon_cp() const override;

    void on_enter() override;
    void on_exit()  override;
    void paint(gfx::Canvas& canvas) override;
    shell::InputResult on_input(const ::app::ebook::input::Event& ev) override;
    void on_ui_event(const ui::UiEvent& ev) override;
    bool on_semantic_back() override;

    void bt_apply_state();
    void hotspot_apply_state();

    struct WifiAp
    {
        char   ssid[33]{};
        int8_t rssi{0};
        bool   encrypted{false};
    };

    enum class WifiRowKind : uint8_t
    {
        Toggle = 0,
        SavedAp,
        ScanTrigger,
        ScannedAp,
    };

    struct WifiRowDesc
    {
        WifiRowKind kind;
        uint8_t     index;
    };

  private:
    SettingsApp();

    void enter_page(SettingsPage p);
    void enter_keys_edit(::app::ebook::input::InputContext ctx);
    void exit_current_page();
    bool back_to_menu();

    void paint_menu(gfx::Canvas& c);
    void paint_wifi(gfx::Canvas& c);
    void paint_bluetooth(gfx::Canvas& c);
    void paint_hotspot(gfx::Canvas& c);
    void paint_display(gfx::Canvas& c);
    void paint_keys(gfx::Canvas& c);
    void paint_keys_edit(gfx::Canvas& c);
    void paint_sound(gfx::Canvas& c);
    void paint_time(gfx::Canvas& c);
    void paint_storage(gfx::Canvas& c);
    void paint_security(gfx::Canvas& c);
    void paint_about(gfx::Canvas& c);
    void paint_placeholder(gfx::Canvas& c);

    shell::InputResult handle_menu(const ::app::ebook::input::Event& ev);
    shell::InputResult handle_wifi(const ::app::ebook::input::Event& ev);
    shell::InputResult handle_hotspot(const ::app::ebook::input::Event& ev);
    shell::InputResult handle_bluetooth(const ::app::ebook::input::Event& ev);
    shell::InputResult handle_display(const ::app::ebook::input::Event& ev);
    shell::InputResult handle_keys(const ::app::ebook::input::Event& ev);
    shell::InputResult handle_keys_edit(const ::app::ebook::input::Event& ev);
    shell::InputResult handle_sound(const ::app::ebook::input::Event& ev);
    shell::InputResult handle_time(const ::app::ebook::input::Event& ev);
    shell::InputResult handle_security(const ::app::ebook::input::Event& ev);
    shell::InputResult handle_about(const ::app::ebook::input::Event& ev);

    void on_wifi_state(uint8_t state, uint8_t fail);
    void on_wifi_scan_done();
    void on_ntp_sync_done(uint8_t status);

    void bt_enter();
    void bt_leave();

    void wifi_enter();
    void wifi_leave();
    void wifi_start_scan();
    void wifi_refresh_saved();
    void wifi_connect_to(uint8_t scan_idx, const char* password);
    void wifi_forget(const char* ssid);
    void wifi_reconnect_saved(const char* ssid);
    uint8_t build_wifi_rows(WifiRowDesc* out, uint8_t cap) const;

    void try_ntp_auto_sync();
    void try_ntp_auto_sync_on_connect(uint8_t wifi_state);
    static bool ensure_ntp_started();

    static void on_hotspot_name_done(const char* text, void* user);
    static void on_hotspot_pwd_done(const char* text, void* user);
    static void on_wifi_pwd_done(const char* text, void* user);
    static void on_bt_name_done(const char* text, void* user);
    static void on_sec_pin_done(const char* text, void* user);

    void open_pin_keyboard(const char* title);
    void begin_set_pin_flow(bool require_old);
    void on_sec_pin_done_impl(const char* text);

    void load_hotspot_prefs();
    void save_hotspot_prefs();
    void load_time_prefs();
    void save_time_prefs();
    void apply_tz_from_index();

    static constexpr uint8_t kMaxScan  = 16;
    static constexpr uint8_t kMaxSaved = 8;

    SettingsPage page_{SettingsPage::Menu};
    ::app::ebook::input::InputContext keys_edit_ctx_{::app::ebook::input::InputContext::Reader};
    ui::ListView menu_list_{};

    bool    wifi_scanning_{false};
    uint8_t wifi_scan_count_{0};
    uint8_t wifi_saved_count_{0};
    uint8_t wifi_pending_idx_{0xFF};
    uint8_t wifi_state_{0};
    uint8_t wifi_scroll_row_{0};
    char    wifi_pending_ssid_[33]{};
    char    wifi_connected_ssid_[33]{};
    WifiAp  wifi_scan_[kMaxScan]{};
    WifiAp  wifi_saved_[kMaxSaved]{};

    char hotspot_name_[33]{"Ebook_AP"};
    char hotspot_pwd_[33]{"12345678"};
    char bt_name_[33]{"Ebook-BLE"};

    uint8_t tz_idx_{20};
    int8_t  tz_offset_h_{8};
    bool    ntp_auto_sync_{true};

    uint8_t about_scroll_row_{0};

    SecPinPurpose sec_pin_purpose_{SecPinPurpose::None};
    char          sec_pending_pin_[5]{};
};

} // namespace app::ebook::apps::settings
