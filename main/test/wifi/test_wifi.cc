#include "test_wifi.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

#include "esp_log.h"
#include "esp_wifi_types.h"
#include "nvs_flash.h"

#include "wifi.hpp"
#include "system/event/event.hpp"
#include "system/task/task.hpp"
#include "test_log.hpp"

namespace {
const char* const TAG = "test_wifi";

constexpr uint32_t k_scan_wait_ms = 5000;
constexpr uint32_t k_connect_wait_ms = 20000;
constexpr uint32_t k_poll_step_ms = 100;

using W = app::network::wifi::WiFiMgr;
using State = app::network::wifi::State;
using FailReason = app::network::wifi::FailureReason;
using Credentials = app::network::wifi::Credentials;
using Info = app::network::wifi::Info;

bool cred_tests_enabled(void)
{
    return WIFI_TEST_SSID[0] != '\0';
}

const char* state_str_zh(State s)
{
    switch (s)
    {
        case State::DISCONNECTED:
            return "已断开";
        case State::CONNECTING:
            return "连接中";
        case State::CONNECTED:
            return "已连接";
        case State::FAILED:
            return "失败";
        default:
            return "未知";
    }
}

const char* reason_str_zh(FailReason r)
{
    switch (r)
    {
        case FailReason::NONE:
            return "无";
        case FailReason::TIMEOUT:
            return "超时";
        case FailReason::WRONG_PASSWORD:
            return "密码错误";
        case FailReason::NETWORK_NOT_FOUND:
            return "找不到网络";
        case FailReason::CONNECTION_FAILED:
            return "连接失败";
        case FailReason::UNKNOWN:
            return "未知原因";
        default:
            return "未知";
    }
}

const char* authmode_label_zh(uint8_t raw)
{
    switch (static_cast<wifi_auth_mode_t>(raw))
    {
        case WIFI_AUTH_OPEN:
            return "开放";
        case WIFI_AUTH_WEP:
            return "WEP";
        case WIFI_AUTH_WPA_PSK:
            return "WPA_PSK";
        case WIFI_AUTH_WPA2_PSK:
            return "WPA2_PSK";
        case WIFI_AUTH_WPA_WPA2_PSK:
            return "WPA/WPA2_PSK";
        case WIFI_AUTH_WPA2_ENTERPRISE:
            return "WPA2_企业";
        case WIFI_AUTH_WPA3_PSK:
            return "WPA3_PSK";
        case WIFI_AUTH_WPA2_WPA3_PSK:
            return "WPA2/WPA3_PSK";
        case WIFI_AUTH_WAPI_PSK:
            return "WAPI_PSK";
        default:
            return "其它";
    }
}

void log_scan_table_header(void)
{
    app::test::log_kv(TAG, "表头", "序号 | 信号 | 加密 | 认证 | BSSID | SSID");
    ESP_LOGI(TAG, "  序号 | 信号(dBm) | 加密 | 认证(值,名称)   | BSSID            | SSID");
    ESP_LOGI(TAG,
             "  -----+-----------+-----+------------------+------------------+------------------");
}

void log_ap_row(unsigned idx, const app::network::wifi::ApInfo& ap)
{
    char bssid[24];
    std::snprintf(bssid, sizeof(bssid), "%02x:%02x:%02x:%02x:%02x:%02x", ap.bssid[0], ap.bssid[1],
                  ap.bssid[2], ap.bssid[3], ap.bssid[4], ap.bssid[5]);

    ESP_LOGI(TAG, "  %3u | %4d      | %3u | %2u %-14s | %-16s | %s", static_cast<unsigned>(idx),
             static_cast<int>(ap.rssi), ap.encrypted ? 1U : 0U, static_cast<unsigned>(ap.authmode),
             authmode_label_zh(ap.authmode), bssid, ap.ssid);
}

void log_info_block(const char* snapshot_title_zh, const Info& info, bool is_connected)
{
    app::test::log_section_begin(TAG, snapshot_title_zh);
    app::test::log_kv(TAG, "状态", state_str_zh(info.state));
    app::test::log_kv(TAG, "失败原因", reason_str_zh(info.fail_reason));
    app::test::log_kv(TAG, "SSID", info.ssid);
    app::test::log_kv_fmt(TAG, "IPv4 地址", "%u.%u.%u.%u", static_cast<unsigned>(info.ip[0]),
                          static_cast<unsigned>(info.ip[1]), static_cast<unsigned>(info.ip[2]),
                          static_cast<unsigned>(info.ip[3]));
    app::test::log_kv_fmt(TAG, "信号强度", "%d dBm", static_cast<int>(info.rssi));
    app::test::log_kv(TAG, "是否已连接", is_connected ? "是" : "否");
    app::test::log_section_end(TAG);
}

bool wait_state(W& wifi, State want, uint32_t timeout_ms)
{
    uint32_t elapsed = 0;
    while (elapsed < timeout_ms)
    {
        if (wifi.get_state() == want)
        {
            return true;
        }
        app::sys::task::TaskMgr::delay_ms(k_poll_step_ms);
        elapsed += k_poll_step_ms;
    }
    return wifi.get_state() == want;
}

bool nvs_boot(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "nvs_flash_init 失败： %s", esp_err_to_name(ret));
        return false;
    }
    return true;
}

void phase_scan(W& wifi)
{
    app::test::log_section_begin(TAG, "[1] 扫描周边接入点");
    app::test::log_kv(TAG, "步骤", "调用 WiFiMgr::scan()");
    if (!wifi.scan([](const std::vector<app::network::wifi::ApInfo>& aps) {
            const unsigned n = static_cast<unsigned>(aps.size());
            app::test::log_kv_fmt(TAG, "发现接入点数", "%u", n);
            log_scan_table_header();
            const unsigned show = n < 24U ? n : 24U;
            for (unsigned i = 0; i < show; i++)
            {
                log_ap_row(i, aps[i]);
            }
            ESP_LOGI(TAG, "  "
                          "-----+-----------+-----+------------------+------------------+------"
                          "------------");
            if (n > show)
            {
                app::test::log_kv_fmt(TAG, "说明", "另有 %u 个 AP 未列出（最多显示 24 个）",
                                      n - show);
            }
            if (n > 0U && show > 0U)
            {
                const auto& best = aps[0];
                app::test::log_kv_fmt(TAG, "信号最强", "%s  %d dBm", best.ssid,
                                      static_cast<int>(best.rssi));
            }
        }))
    {
        app::test::log_kv(TAG, "结果", "失败：scan 未接受回调");
        app::test::log_section_end(TAG);
        return;
    }
    app::sys::task::TaskMgr::delay_ms(k_scan_wait_ms);
    app::test::log_kv_fmt(TAG, "扫描后等待", "%u ms（期间已输出 AP 表）",
                          static_cast<unsigned>(k_scan_wait_ms));
    app::test::log_section_end(TAG);
}

void phase_creds_roundtrip(W& wifi)
{
    app::test::log_section_begin(TAG, "[2] 清空 NVS 中的 WiFi 凭据");
    app::test::log_kv(TAG, "步骤", "WiFiMgr::clear_creds()");
    if (!wifi.clear_creds())
    {
        app::test::log_kv(TAG, "结果", "提示：clear_creds 返回 false（NVS 为空时可接受）");
    }
    else
    {
        app::test::log_kv(TAG, "结果", "成功");
    }
    app::sys::task::TaskMgr::delay_ms(200);
    app::test::log_section_end(TAG);

    app::test::log_section_begin(TAG, "[3] 查询是否已有保存的凭据");
    app::test::log_kv(TAG, "has_saved_creds", wifi.has_saved_creds() ? "是" : "否");
    app::test::log_section_end(TAG);

    if (!cred_tests_enabled())
    {
        return;
    }

    app::test::log_section_begin(TAG, "[4] 将测试 SSID/密码写入 NVS");
    app::test::log_kv(TAG, "测试 SSID", WIFI_TEST_SSID);
    app::test::log_kv(TAG, "密码", strlen(WIFI_TEST_PASSWORD) > 0 ? "（非空）" : "（空）");
    app::test::log_kv(TAG, "步骤", "WiFiMgr::save_creds()");
    Credentials c(WIFI_TEST_SSID, WIFI_TEST_PASSWORD);
    if (!wifi.save_creds(c))
    {
        app::test::log_kv(TAG, "结果", "失败：save_creds");
    }
    else
    {
        app::test::log_kv(TAG, "结果", "成功");
    }
    app::sys::task::TaskMgr::delay_ms(200);
    app::test::log_section_end(TAG);

    app::test::log_section_begin(TAG, "[5] 从 NVS 读取凭据列表");
    std::vector<Credentials> loaded;
    if (!wifi.get_creds(loaded))
    {
        app::test::log_kv(TAG, "结果", "失败：get_creds");
    }
    else
    {
        app::test::log_kv_fmt(TAG, "条目数", "%u", static_cast<unsigned>(loaded.size()));
        for (unsigned i = 0; i < static_cast<unsigned>(loaded.size()); i++)
        {
            app::test::log_kv_fmt(TAG, "条目", "[%u] SSID=%s", i, loaded[i].ssid);
        }
    }
    app::test::log_section_end(TAG);
}

void phase_connect_explicit(W& wifi)
{
    if (!cred_tests_enabled())
    {
        app::test::log_section_begin(TAG, "[6–9] 跳过联网测试");
        app::test::log_kv(TAG, "原因", "WIFI_TEST_SSID 为空（请在 test_wifi.hpp 中填写）");
        app::test::log_section_end(TAG);
        return;
    }

    app::test::log_section_begin(TAG, "[6] 使用指定 SSID/密码连接");
    app::test::log_kv(TAG, "目标 SSID", WIFI_TEST_SSID);
    app::test::log_kv(TAG, "步骤", "disconnect() 后 connect()");
    wifi.disconnect();
    app::sys::task::TaskMgr::delay_ms(300);

    if (!wifi.connect(WIFI_TEST_SSID, WIFI_TEST_PASSWORD, 45000))
    {
        app::test::log_kv(TAG, "结果", "失败：connect 未启动");
        log_info_block("链路信息 · 显式连接未启动", wifi.get_info(), wifi.is_connected());
        app::test::log_section_end(TAG);
        return;
    }

    app::test::log_kv_fmt(TAG, "等待已连接", "超时 %u ms",
                          static_cast<unsigned>(k_connect_wait_ms));
    const bool ok = wait_state(wifi, State::CONNECTED, k_connect_wait_ms);
    log_info_block(ok ? "链路信息 · 显式连接成功" : "链路信息 · 显式连接超时", wifi.get_info(),
                   wifi.is_connected());
    app::test::log_kv(TAG, "结果", ok ? "通过：已关联 AP" : "失败：请检查 SSID/密码/路由器");
    app::test::log_section_end(TAG);
}

void phase_disconnect(W& wifi)
{
    if (!cred_tests_enabled())
    {
        return;
    }

    app::test::log_section_begin(TAG, "[7] 主动断开连接");
    app::test::log_kv(TAG, "步骤", "WiFiMgr::disconnect()");
    wifi.disconnect();
    app::sys::task::TaskMgr::delay_ms(1500);
    app::test::log_kv(TAG, "当前状态", state_str_zh(wifi.get_state()));
    app::test::log_section_end(TAG);
}

void phase_connect_saved(W& wifi)
{
    if (!cred_tests_enabled())
    {
        return;
    }

    app::test::log_section_begin(TAG, "[8] 使用已保存凭据连接");
    app::test::log_kv(TAG, "步骤", "connect(nullptr, nullptr)");
    if (!wifi.connect(nullptr, nullptr, 45000))
    {
        app::test::log_kv(TAG, "结果", "失败：connect(已存) 未启动");
        app::test::log_section_end(TAG);
        return;
    }
    app::test::log_kv_fmt(TAG, "等待已连接", "超时 %u ms",
                          static_cast<unsigned>(k_connect_wait_ms));
    const bool ok = wait_state(wifi, State::CONNECTED, k_connect_wait_ms);
    log_info_block(ok ? "链路信息 · 已存凭据连接成功" : "链路信息 · 已存凭据连接超时",
                   wifi.get_info(), wifi.is_connected());
    app::test::log_kv(TAG, "结果", ok ? "通过" : "失败");
    app::test::log_section_end(TAG);

    app::test::log_section_begin(TAG, "[9] 删除测试 SSID 对应凭据");
    app::test::log_kv(TAG, "步骤", "remove_creds(测试 SSID)");
    if (!wifi.remove_creds(WIFI_TEST_SSID))
    {
        app::test::log_kv(TAG, "结果", "提示：remove_creds 返回 false");
    }
    else
    {
        app::test::log_kv_fmt(TAG, "结果", "已移除 %s", WIFI_TEST_SSID);
    }
    app::sys::task::TaskMgr::delay_ms(200);
    app::test::log_section_end(TAG);
}

} // namespace

extern "C" void test_wifi(void)
{
    app::test::log_section_begin(TAG, "WiFi 功能测试 · 开始");
    app::test::log_kv(TAG, "目标 SSID（配置）", WIFI_TEST_SSID);
    app::test::log_kv(TAG, "凭据类子测试", cred_tests_enabled() ? "开启" : "关闭");
    app::test::log_section_end(TAG);

    if (!nvs_boot())
    {
        return;
    }

    app::test::log_section_begin(TAG, "初始化 EventMgr");
    auto& event_mgr = app::sys::event::EventMgr::get_instance();
    if (!event_mgr.init())
    {
        app::test::log_kv(TAG, "结果", "失败：EventMgr::init");
        app::test::log_section_end(TAG);
        return;
    }
    app::test::log_kv(TAG, "结果", "成功");
    app::test::log_section_end(TAG);

    W& wifi = W::get_instance();

    app::test::log_section_begin(TAG, "初始化 WiFiMgr");
    wifi.set_state_cb([](State state, FailReason reason) {
        ESP_LOGD(TAG, "  状态回调： %s | %s", state_str_zh(state), reason_str_zh(reason));
    });

    if (!wifi.init())
    {
        app::test::log_kv(TAG, "结果", "失败：WiFiMgr::init");
        app::test::log_section_end(TAG);
        return;
    }
    app::test::log_kv(TAG, "结果", "成功");
    app::sys::task::TaskMgr::delay_ms(400);
    app::test::log_section_end(TAG);

    phase_scan(wifi);
    phase_creds_roundtrip(wifi);
    phase_connect_explicit(wifi);
    phase_disconnect(wifi);
    phase_connect_saved(wifi);

    app::test::log_section_begin(TAG, "[10] 清理（断开并清空 NVS 凭据）");
    app::test::log_kv(TAG, "步骤", "disconnect + clear_creds");
    wifi.disconnect();
    app::sys::task::TaskMgr::delay_ms(500);
    if (!wifi.clear_creds())
    {
        app::test::log_kv(TAG, "结果", "提示：clear_creds 返回 false");
    }
    else
    {
        app::test::log_kv(TAG, "结果", "成功：NVS 已清空");
    }
    app::test::log_section_end(TAG);

    app::test::log_section_begin(TAG, "WiFi 功能测试 · 结束");
    app::test::log_kv(TAG, "状态", "已完成");
    app::test::log_section_end(TAG);
}
