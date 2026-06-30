#include "wifi.hpp"

#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <esp_log.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <nvs.h>

static const char* const TAG = "Wifi";

namespace {

const char* NVS_NS = "wifi";
const char* NVS_LIST = "list";

std::string build_pass_key(const char* ssid)
{
    std::string key = "pass:";
    key += ssid;
    return key;
}

bool get_ssid_list(std::vector<std::string>& ssids)
{
    nvs_handle_t h;
    esp_err_t ret = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (ret != ESP_OK)
        return false;

    size_t sz = 0;
    ret = nvs_get_str(h, NVS_LIST, nullptr, &sz);
    if (ret != ESP_OK || sz == 0)
    {
        nvs_close(h);
        return false;
    }

    std::vector<char> buf(sz);
    ret = nvs_get_str(h, NVS_LIST, buf.data(), &sz);
    nvs_close(h);

    if (ret != ESP_OK)
        return false;

    ssids.clear();
    char* tok = strtok(buf.data(), ",");
    while (tok)
    {
        ssids.push_back(std::string(tok));
        tok = strtok(nullptr, ",");
    }

    return true;
}

bool update_ssid_list(const std::vector<std::string>& ssids)
{
    if (ssids.empty())
    {
        nvs_handle_t h;
        esp_err_t ret = nvs_open(NVS_NS, NVS_READWRITE, &h);
        if (ret == ESP_OK)
        {
            nvs_erase_key(h, NVS_LIST);
            nvs_commit(h);
            nvs_close(h);
        }
        return true;
    }

    std::string list;
    for (size_t i = 0; i < ssids.size(); i++)
    {
        if (i > 0)
            list += ",";
        list += ssids[i];
    }

    nvs_handle_t h;
    esp_err_t ret = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (ret != ESP_OK)
        return false;

    ret = nvs_set_str(h, NVS_LIST, list.c_str());
    if (ret == ESP_OK)
        ret = nvs_commit(h);
    nvs_close(h);

    return ret == ESP_OK;
}

} // namespace

namespace app::network::wifi {

WiFiMgr& WiFiMgr::get_instance()
{
    static WiFiMgr s;
    return s;
}

WiFiMgr::WiFiMgr()
    : init_(false)
    , conn_timeout_ms_(0)
    , conn_timeout_set_(false)
    , conn_start_tick_(0)
    , disconnecting_(false)
    , scanning_(false)
{
}

WiFiMgr::~WiFiMgr()
{
    if (init_)
        deinit();
}

bool WiFiMgr::init()
{
    std::lock_guard<std::mutex> lk(mtx_);
    if (init_)
        return true;

    nvs_handle_t h;
    esp_err_t ret = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (ret == ESP_ERR_NVS_NOT_INITIALIZED)
    {
        ESP_LOGE(TAG, "NVS 未初始化，请在启动时调用 nvs_flash_init()");
        return false;
    }
    if (ret == ESP_OK)
        nvs_close(h);

    auto& evt = app::sys::event::EventMgr::get_instance();
    if (!evt.is_init())
    {
        if (!evt.init())
        {
            ESP_LOGE(TAG, "EventMgr 未就绪");
            return false;
        }
    }

    ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE(TAG, "esp_netif_init: %s", esp_err_to_name(ret));
        return false;
    }
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_wifi_init: %s", esp_err_to_name(ret));
        return false;
    }

    evt.register_handler(
        WIFI_EVENT, ESP_EVENT_ANY_ID,
        [this](esp_event_base_t base, app::sys::event::EventId id,
               const app::sys::event::EventData& data) { handle_wifi_event(base, id, data); });

    evt.register_handler(
        IP_EVENT, IP_EVENT_STA_GOT_IP,
        [this](esp_event_base_t base, app::sys::event::EventId id,
               const app::sys::event::EventData& data) { handle_ip_event(base, id, data); });

    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_wifi_set_mode: %s", esp_err_to_name(ret));
        esp_wifi_deinit();
        return false;
    }
    ret = esp_wifi_start();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_wifi_start: %s", esp_err_to_name(ret));
        esp_wifi_deinit();
        return false;
    }

    init_ = true;
    ESP_LOGI(TAG, "已启动 STA 模式");
    info_.state = State::DISCONNECTED;
    conn_timeout_set_ = false;

    return true;
}

void WiFiMgr::deinit()
{
    std::unique_lock<std::mutex> lk(mtx_);
    if (!init_)
        return;

    lk.unlock();
    disconnect();
    app::sys::task::TaskMgr::delay_ms(50);
    lk.lock();

    esp_wifi_stop();
    esp_wifi_deinit();

    auto& evt = app::sys::event::EventMgr::get_instance();
    evt.unregister_handler(WIFI_EVENT, ESP_EVENT_ANY_ID);
    evt.unregister_handler(IP_EVENT, IP_EVENT_STA_GOT_IP);

    init_ = false;
}

bool WiFiMgr::scan(ScanCb cb)
{
    std::lock_guard<std::mutex> lk(mtx_);
    if (!init_ || scanning_)
        return false;

    scan_cb_ = cb;
    scanning_ = true;

    wifi_scan_config_t cfg = {};
    cfg.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    cfg.scan_time.active.min = 100;
    cfg.scan_time.active.max = 300;

    esp_err_t ret = esp_wifi_scan_start(&cfg, false);
    if (ret != ESP_OK)
    {
        scanning_ = false;
        return false;
    }

    return true;
}

bool WiFiMgr::connect(const char* ssid, const char* password, uint32_t timeout_ms)
{
    std::unique_lock<std::mutex> lk(mtx_);
    if (!init_)
        return false;

    Credentials creds;

    if (ssid && ssid[0] != '\0')
    {
        creds.set_ssid(ssid);
        if (password)
            creds.set_password(password);
    }
    else
    {
        lk.unlock();

        std::vector<Credentials> saved;
        if (!get_creds(saved) || saved.empty())
            return false;

        if (saved.size() == 1)
        {
            creds = saved[0];
        }
        else
        {
            wifi_scan_config_t cfg = {};
            cfg.scan_type = WIFI_SCAN_TYPE_ACTIVE;
            cfg.scan_time.active.min = 100;
            cfg.scan_time.active.max = 300;

            esp_err_t ret = esp_wifi_scan_start(&cfg, true);
            if (ret != ESP_OK)
            {
                creds = saved[0];
            }
            else
            {
                uint16_t cnt = 0;
                esp_wifi_scan_get_ap_num(&cnt);

                Credentials best;
                int8_t best_rssi = -100;

                if (cnt > 0)
                {
                    std::vector<wifi_ap_record_t> recs(cnt);
                    esp_wifi_scan_get_ap_records(&cnt, recs.data());

                    for (uint16_t i = 0; i < cnt; i++)
                    {
                        const char* ap_ssid = reinterpret_cast<const char*>(recs[i].ssid);
                        for (const auto& s : saved)
                        {
                            if (strcmp(ap_ssid, s.ssid) == 0)
                            {
                                if (recs[i].rssi > best_rssi)
                                {
                                    best_rssi = recs[i].rssi;
                                    best = s;
                                }
                                break;
                            }
                        }
                    }
                }

                creds = best.valid() ? best : saved[0];
            }
        }

        lk.lock();
    }

    if (!creds.valid())
        return false;

    if (info_.state == State::CONNECTING || info_.state == State::CONNECTED)
    {
        if (strcmp(info_.ssid, creds.ssid) == 0)
            return true;
        lk.unlock();
        disconnect();
        app::sys::task::TaskMgr::delay_ms(50);
        lk.lock();
    }

    wifi_config_t wcfg = {};
    strncpy(reinterpret_cast<char*>(wcfg.sta.ssid), creds.ssid, sizeof(wcfg.sta.ssid) - 1);
    strncpy(reinterpret_cast<char*>(wcfg.sta.password), creds.password,
            sizeof(wcfg.sta.password) - 1);
    wcfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wcfg.sta.pmf_cfg.capable = true;
    wcfg.sta.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wcfg));

    conn_timeout_ms_ = timeout_ms;
    conn_timeout_set_ = true;
    conn_start_tick_ = app::sys::task::TaskMgr::tick_count();

    update_state(State::CONNECTING);

    if (timeout_ms > 0 && !timeout_task_)
    {
        app::sys::task::Cfg cfg;
        cfg.name = "wifi_to";
        cfg.stack_size = 2048;
        cfg.priority = app::sys::task::Priority::NORMAL;
        cfg.core_id = -1;

        timeout_task_ = std::make_unique<app::sys::task::Task>(timeout_task_fn, cfg, this);
        if (!timeout_task_->start())
        {
            ESP_LOGE(TAG, "超时任务创建失败");
            timeout_task_.reset();
        }
    }

    esp_err_t ret = esp_wifi_connect();
    if (ret != ESP_OK)
    {
        update_state(State::FAILED, FailureReason::CONNECTION_FAILED);
        clear_timeout_task();
        return false;
    }

    return true;
}

void WiFiMgr::disconnect()
{
    std::lock_guard<std::mutex> lk(mtx_);
    if (!init_ || info_.state == State::DISCONNECTED)
        return;

    disconnecting_ = true;
    conn_timeout_set_ = false;
    clear_timeout_task();

    esp_wifi_disconnect();
}

const Info& WiFiMgr::get_info() const
{
    std::lock_guard<std::mutex> lk(mtx_);
    return info_;
}

State WiFiMgr::get_state() const
{
    std::lock_guard<std::mutex> lk(mtx_);
    return info_.state;
}

bool WiFiMgr::is_connected() const
{
    std::lock_guard<std::mutex> lk(mtx_);
    return info_.state == State::CONNECTED;
}

void WiFiMgr::set_state_cb(StateCb cb)
{
    std::lock_guard<std::mutex> lk(mtx_);
    state_cb_ = std::move(cb);
}

bool WiFiMgr::save_creds(const Credentials& creds)
{
    if (!creds.valid())
        return false;

    std::vector<std::string> ssids;
    get_ssid_list(ssids);

    bool exists = false;
    for (const auto& s : ssids)
    {
        if (s == creds.ssid)
        {
            exists = true;
            break;
        }
    }

    if (!exists)
    {
        ssids.push_back(std::string(creds.ssid));
        if (!update_ssid_list(ssids))
            return false;
    }

    std::string key = build_pass_key(creds.ssid);
    nvs_handle_t h;
    esp_err_t ret = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (ret != ESP_OK)
        return false;

    ret = nvs_set_str(h, key.c_str(), creds.password);
    if (ret == ESP_OK)
        ret = nvs_commit(h);
    nvs_close(h);

    return ret == ESP_OK;
}

bool WiFiMgr::get_creds(std::vector<Credentials>& creds)
{
    creds.clear();

    std::vector<std::string> ssids;
    if (!get_ssid_list(ssids) || ssids.empty())
        return false;

    nvs_handle_t h;
    esp_err_t ret = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (ret != ESP_OK)
        return false;

    for (const auto& ssid : ssids)
    {
        Credentials c;
        strncpy(c.ssid, ssid.c_str(), sizeof(c.ssid) - 1);
        c.ssid[sizeof(c.ssid) - 1] = '\0';

        std::string key = build_pass_key(ssid.c_str());
        size_t sz = sizeof(c.password);
        ret = nvs_get_str(h, key.c_str(), c.password, &sz);
        if (ret == ESP_OK && c.valid())
        {
            creds.push_back(c);
        }
    }

    nvs_close(h);
    return !creds.empty();
}

bool WiFiMgr::clear_creds()
{
    std::vector<std::string> ssids;
    if (!get_ssid_list(ssids))
        return true;

    nvs_handle_t h;
    esp_err_t ret = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (ret != ESP_OK)
        return false;

    for (const auto& ssid : ssids)
    {
        std::string key = build_pass_key(ssid.c_str());
        nvs_erase_key(h, key.c_str());
    }

    nvs_erase_key(h, NVS_LIST);
    ret = nvs_commit(h);
    nvs_close(h);

    return ret == ESP_OK;
}

bool WiFiMgr::remove_creds(const char* ssid)
{
    if (!ssid || ssid[0] == '\0')
        return false;

    std::vector<std::string> ssids;
    if (!get_ssid_list(ssids))
        return false;

    bool found = false;
    for (auto it = ssids.begin(); it != ssids.end(); ++it)
    {
        if (*it == ssid)
        {
            ssids.erase(it);
            found = true;
            break;
        }
    }

    if (!found)
        return false;

    std::string key = build_pass_key(ssid);
    nvs_handle_t h;
    esp_err_t ret = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (ret != ESP_OK)
        return false;

    nvs_erase_key(h, key.c_str());
    if (!update_ssid_list(ssids))
    {
        nvs_close(h);
        return false;
    }

    ret = nvs_commit(h);
    nvs_close(h);

    std::unique_lock<std::mutex> lk(mtx_);
    if (info_.state == State::CONNECTED || info_.state == State::CONNECTING)
    {
        if (strcmp(info_.ssid, ssid) == 0)
        {
            lk.unlock();
            disconnect();
            app::sys::task::TaskMgr::delay_ms(50);
        }
    }

    return ret == ESP_OK;
}

bool WiFiMgr::has_saved_creds()
{
    std::vector<std::string> ssids;
    return get_ssid_list(ssids) && !ssids.empty();
}

bool WiFiMgr::persist_sta_creds()
{
    wifi_config_t wcfg = {};
    if (esp_wifi_get_config(WIFI_IF_STA, &wcfg) != ESP_OK)
        return false;

    const char* ssid = reinterpret_cast<const char*>(wcfg.sta.ssid);
    if (ssid[0] == '\0')
        return false;

    Credentials c;
    c.set_ssid(ssid);
    c.set_password(reinterpret_cast<const char*>(wcfg.sta.password));
    return save_creds(c);
}

void WiFiMgr::handle_wifi_event(esp_event_base_t base, app::sys::event::EventId id,
                                const app::sys::event::EventData& data)
{
    std::unique_lock<std::mutex> lk(mtx_);

    switch (id)
    {
        case WIFI_EVENT_STA_START:
            break;

        case WIFI_EVENT_STA_CONNECTED: {
            auto* evt = static_cast<wifi_event_sta_connected_t*>(data.data);
            strncpy(info_.ssid, reinterpret_cast<const char*>(evt->ssid), sizeof(info_.ssid) - 1);
            info_.ssid[sizeof(info_.ssid) - 1] = '\0';

            lk.unlock();
            wifi_ap_record_t ap;
            esp_err_t ret = esp_wifi_sta_get_ap_info(&ap);
            lk.lock();

            info_.rssi = (ret == ESP_OK) ? ap.rssi : 0;
            break;
        }

        case WIFI_EVENT_STA_DISCONNECTED: {
            auto* evt = static_cast<wifi_event_sta_disconnected_t*>(data.data);

            conn_timeout_set_ = false;
            clear_timeout_task();

            if (disconnecting_)
            {
                disconnecting_ = false;
                update_state(State::DISCONNECTED);
                break;
            }

            FailureReason reason = FailureReason::UNKNOWN;
            if (evt->reason == WIFI_REASON_AUTH_EXPIRE ||
                evt->reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
                evt->reason == WIFI_REASON_AUTH_FAIL)
            {
                reason = FailureReason::WRONG_PASSWORD;
            }
            else if (evt->reason == WIFI_REASON_NO_AP_FOUND)
            {
                reason = FailureReason::NETWORK_NOT_FOUND;
            }
            else if (evt->reason == WIFI_REASON_ASSOC_FAIL)
            {
                reason = FailureReason::CONNECTION_FAILED;
            }

            if (info_.state == State::CONNECTING)
            {
                update_state(State::FAILED, reason);
            }
            else
            {
                update_state(State::DISCONNECTED);
            }
            break;
        }

        case WIFI_EVENT_SCAN_DONE: {
            scanning_ = false;

            uint16_t cnt = 0;
            esp_wifi_scan_get_ap_num(&cnt);

            ScanCb cb = scan_cb_;
            std::vector<ApInfo> aps;

            if (cnt > 0)
            {
                aps.reserve(cnt);
                std::vector<wifi_ap_record_t> recs(cnt);
                esp_wifi_scan_get_ap_records(&cnt, recs.data());

                for (uint16_t i = 0; i < cnt; i++)
                {
                    ApInfo a;
                    strncpy(a.ssid, reinterpret_cast<const char*>(recs[i].ssid),
                            sizeof(a.ssid) - 1);
                    a.ssid[sizeof(a.ssid) - 1] = '\0';
                    memcpy(a.bssid, recs[i].bssid, sizeof(a.bssid));
                    a.rssi = recs[i].rssi;
                    a.authmode = recs[i].authmode;
                    a.encrypted = (recs[i].authmode != WIFI_AUTH_OPEN);
                    aps.push_back(a);
                }
            }

            lk.unlock();
            if (cb)
                cb(aps);
            break;
        }

        default:
            break;
    }
}

void WiFiMgr::handle_ip_event(esp_event_base_t base, app::sys::event::EventId id,
                              const app::sys::event::EventData& data)
{
    std::unique_lock<std::mutex> lk(mtx_);

    if (id == IP_EVENT_STA_GOT_IP)
    {
        auto* evt = static_cast<ip_event_got_ip_t*>(data.data);
        memcpy(info_.ip, &evt->ip_info.ip.addr, 4);
        conn_timeout_set_ = false;
        clear_timeout_task();
        update_state(State::CONNECTED);

        lk.unlock();
        (void)persist_sta_creds();
    }
}

void WiFiMgr::clear_timeout_task()
{
    if (timeout_task_)
    {
        timeout_task_->destroy();
        timeout_task_.reset();
    }
}

void WiFiMgr::update_state(State state, FailureReason reason)
{
    if (info_.state != state || info_.fail_reason != reason)
    {
        info_.state = state;
        info_.fail_reason = reason;
        StateCb cb = state_cb_;

        if (cb)
        {
            mtx_.unlock();
            cb(state, reason);
            mtx_.lock();
        }
    }
}

void WiFiMgr::check_timeout()
{
    std::unique_lock<std::mutex> lk(mtx_);

    if (!conn_timeout_set_ || info_.state != State::CONNECTING)
        return;

    uint32_t elapsed =
        (app::sys::task::TaskMgr::tick_count() - conn_start_tick_) * portTICK_PERIOD_MS;

    if (elapsed >= conn_timeout_ms_)
    {
        conn_timeout_set_ = false;
        StateCb cb = state_cb_;

        info_.state = State::FAILED;
        info_.fail_reason = FailureReason::TIMEOUT;

        lk.unlock();
        if (cb)
            cb(State::FAILED, FailureReason::TIMEOUT);
        esp_wifi_disconnect();
    }
}

void WiFiMgr::timeout_task_fn(void* arg)
{
    WiFiMgr* self = static_cast<WiFiMgr*>(arg);
    if (!self)
        return;

    while (true)
    {
        app::sys::task::TaskMgr::delay_ms(1000);
        self->check_timeout();

        std::lock_guard<std::mutex> lk(self->mtx_);
        if (!self->conn_timeout_set_ || self->info_.state != State::CONNECTING)
        {
            break;
        }
    }
}

bool WiFiMgr::enable_ap(const char* ssid, const char* password, uint8_t channel)
{
    std::lock_guard<std::mutex> lk(mtx_);
    if (!init_ || ssid == nullptr || ssid[0] == '\0') return false;

    // 创建默认 AP netif（幂等，重复调用不会出错；首次时建立 DHCP server）
    static bool ap_netif_created = false;
    if (!ap_netif_created)
    {
        esp_netif_create_default_wifi_ap();
        ap_netif_created = true;
    }

    esp_err_t r = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (r != ESP_OK)
    {
        ESP_LOGE(TAG, "set_mode APSTA: %s", esp_err_to_name(r));
        return false;
    }

    wifi_config_t ap_cfg = {};
    std::strncpy(reinterpret_cast<char*>(ap_cfg.ap.ssid),
                 ssid, sizeof(ap_cfg.ap.ssid) - 1);
    ap_cfg.ap.ssid_len      = static_cast<uint8_t>(std::strlen(ssid));
    ap_cfg.ap.channel       = (channel == 0 || channel > 13) ? 6 : channel;
    ap_cfg.ap.max_connection = 4;
    ap_cfg.ap.beacon_interval = 100;

    const size_t pwd_len = (password != nullptr) ? std::strlen(password) : 0;
    if (pwd_len >= 8)
    {
        std::strncpy(reinterpret_cast<char*>(ap_cfg.ap.password),
                     password, sizeof(ap_cfg.ap.password) - 1);
        ap_cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    }
    else
    {
        ap_cfg.ap.authmode = WIFI_AUTH_OPEN;
    }

    r = esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
    if (r != ESP_OK)
    {
        ESP_LOGE(TAG, "set_config AP: %s", esp_err_to_name(r));
        return false;
    }

    ap_active_ = true;
    ESP_LOGI(TAG, "SoftAP enabled: SSID='%s' ch=%u auth=%s",
             ssid, ap_cfg.ap.channel,
             (ap_cfg.ap.authmode == WIFI_AUTH_OPEN) ? "OPEN" : "WPA2");
    return true;
}

bool WiFiMgr::disable_ap()
{
    std::lock_guard<std::mutex> lk(mtx_);
    if (!init_) return false;
    if (!ap_active_) return true;

    const esp_err_t r = esp_wifi_set_mode(WIFI_MODE_STA);
    if (r != ESP_OK)
    {
        ESP_LOGW(TAG, "set_mode STA: %s", esp_err_to_name(r));
        return false;
    }
    ap_active_ = false;
    ESP_LOGI(TAG, "SoftAP disabled");
    return true;
}

bool WiFiMgr::is_ap_active() const
{
    std::lock_guard<std::mutex> lk(mtx_);
    return ap_active_;
}

uint8_t WiFiMgr::get_ap_sta_count() const
{
    std::lock_guard<std::mutex> lk(mtx_);
    if (!ap_active_) return 0;

    wifi_sta_list_t list{};
    if (esp_wifi_ap_get_sta_list(&list) != ESP_OK) return 0;
    return list.num;
}

} // namespace app::network::wifi
