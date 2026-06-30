#pragma once

#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include "system/event/event.hpp"
#include "system/task/task.hpp"

namespace app::network::wifi {

enum class State
{
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    FAILED,
};

enum class FailureReason
{
    NONE,
    TIMEOUT,
    WRONG_PASSWORD,
    NETWORK_NOT_FOUND,
    CONNECTION_FAILED,
    UNKNOWN,
};

struct Credentials
{
    char ssid[32]{};
    char password[64]{};

    Credentials() = default;

    Credentials(const char* s, const char* p)
    {
        set_ssid(s);
        set_password(p);
    }

    void set_ssid(const char* s)
    {
        if (s != nullptr)
        {
            strncpy(ssid, s, sizeof(ssid) - 1);
            ssid[sizeof(ssid) - 1] = '\0';
        }
        else
        {
            ssid[0] = '\0';
        }
    }

    void set_password(const char* p)
    {
        if (p != nullptr)
        {
            strncpy(password, p, sizeof(password) - 1);
            password[sizeof(password) - 1] = '\0';
        }
        else
        {
            password[0] = '\0';
        }
    }

    bool valid() const
    {
        return ssid[0] != '\0';
    }
};

struct ApInfo
{
    char ssid[32]{};
    uint8_t bssid[6]{};
    int8_t rssi{};
    uint8_t authmode{};
    bool encrypted{};

    ApInfo() = default;
};

struct Info
{
    State state{State::DISCONNECTED};
    FailureReason fail_reason{FailureReason::NONE};
    char ssid[32]{};
    uint8_t ip[4]{};
    int8_t rssi{};

    Info() = default;
};

using StateCb = std::function<void(State state, FailureReason reason)>;
using ScanCb = std::function<void(const std::vector<ApInfo>& aps)>;

/** @brief WiFi STA + SoftAP；凭证存 NVS 命名空间 wifi */
class WiFiMgr
{
  public:
    static WiFiMgr& get_instance();

    bool init();
    void deinit();

    bool scan(ScanCb cb = nullptr);
    bool connect(const char* ssid = nullptr, const char* password = nullptr,
                 uint32_t timeout_ms = 30000);
    void disconnect();

    const Info& get_info() const;
    State get_state() const;
    bool is_connected() const;

    void set_state_cb(StateCb cb);

    bool save_creds(const Credentials& creds);
    bool get_creds(std::vector<Credentials>& creds);
    bool clear_creds();
    bool remove_creds(const char* ssid);
    bool has_saved_creds();

    /** 将当前 STA 配置（SSID/密码）写入 NVS，供已保存列表展示与重连 */
    bool persist_sta_creds();

    /** APSTA；password≥8 为 WPA2，否则开放 */
    bool enable_ap(const char* ssid, const char* password = nullptr, uint8_t channel = 6);

    bool disable_ap();
    bool is_ap_active() const;
    uint8_t get_ap_sta_count() const;

  private:
    WiFiMgr();
    ~WiFiMgr();
    WiFiMgr(const WiFiMgr&) = delete;
    WiFiMgr& operator=(const WiFiMgr&) = delete;

    void handle_wifi_event(esp_event_base_t base, app::sys::event::EventId id,
                           const app::sys::event::EventData& data);
    void handle_ip_event(esp_event_base_t base, app::sys::event::EventId id,
                         const app::sys::event::EventData& data);

    void update_state(State state, FailureReason reason = FailureReason::NONE);
    void clear_timeout_task();
    void check_timeout();

    static void timeout_task_fn(void* arg);

    mutable std::mutex mtx_;
    bool init_;
    bool ap_active_{false};
    Info info_;
    StateCb state_cb_;
    ScanCb scan_cb_;
    uint32_t conn_timeout_ms_;
    bool conn_timeout_set_;
    uint32_t conn_start_tick_;
    bool disconnecting_;
    bool scanning_;
    std::unique_ptr<app::sys::task::Task> timeout_task_;
};

} // namespace app::network::wifi
