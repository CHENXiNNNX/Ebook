#include "test_bluetooth.hpp"

#include <cstdio>
#include <cstring>

#include "esp_log.h"
#include "nvs_flash.h"

#include "bluetooth.hpp"
#include "gatt.hpp"
#include "system/task/task.hpp"
#include "test_log.hpp"

namespace {

const char* const TAG = "test_bluetooth";

using Ble = app::network::bluetooth::BleMgr;
using BState = app::network::bluetooth::State;
using ConnInfo = app::network::bluetooth::ConnInfo;
using DevInfoSvc = app::network::bluetooth::gatt::DeviceInfoService;
using BattSvc = app::network::bluetooth::gatt::BatteryService;
using ProvisionSvc = app::network::bluetooth::gatt::ProvisionService;
using PStatus = app::network::bluetooth::gatt::ProvisionStatus;
using PCmd = app::network::bluetooth::gatt::ProvisionCommand;

const char* state_str_zh(BState s)
{
    switch (s)
    {
        case BState::UNINIT:
            return "未初始化";
        case BState::IDLE:
            return "空闲";
        case BState::ADVERTISING:
            return "广播中";
        case BState::CONNECTED:
            return "已连接";
        default:
            return "未知";
    }
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

void log_conn(const ConnInfo& c)
{
    char mac[24];
    (void)snprintf(mac, sizeof(mac), "%02x:%02x:%02x:%02x:%02x:%02x", c.address[0], c.address[1],
                   c.address[2], c.address[3], c.address[4], c.address[5]);
    app::test::log_kv_fmt(TAG, "连接句柄", "%u", static_cast<unsigned>(c.conn_handle));
    app::test::log_kv(TAG, "对端地址", mac);
    app::test::log_kv_fmt(TAG, "MTU", "%u", static_cast<unsigned>(c.mtu));
    app::test::log_kv(TAG, "是否已加密", c.encrypted ? "是" : "否");
}

} // namespace

extern "C" void test_bluetooth(void)
{
    app::test::log_section_begin(TAG, "蓝牙功能测试 · 开始");
    app::test::log_kv(TAG, "广播设备名", BLE_TEST_DEVICE_NAME);
    app::test::log_kv(TAG, "说明", "外设模式：GATT 服务 + 可连接广播，可用 nRF Connect 等工具验证");
    app::test::log_section_end(TAG);

    if (!nvs_boot())
    {
        return;
    }

    app::test::log_section_begin(TAG, "[1] 初始化 BleMgr");
    Ble& ble = Ble::get_instance();
    ble.set_state_cb([](BState s) { ESP_LOGI(TAG, "  状态回调： %s", state_str_zh(s)); });
    ble.set_connect_cb([](const ConnInfo& info) {
        app::test::log_section_begin(TAG, "事件 · 对端已连接");
        log_conn(info);
        app::test::log_section_end(TAG);
    });
    ble.set_disconnect_cb([](const ConnInfo& info, int reason) {
        app::test::log_section_begin(TAG, "事件 · 对端已断开");
        app::test::log_kv_fmt(TAG, "断开原因码", "%d", reason);
        log_conn(info);
        app::test::log_section_end(TAG);
    });

    if (!ble.init(BLE_TEST_DEVICE_NAME))
    {
        app::test::log_kv(TAG, "结果", "失败：BleMgr::init");
        app::test::log_section_end(TAG);
        return;
    }
    ble.set_security(app::network::bluetooth::SecMode::NONE);
    app::test::log_kv(TAG, "结果", "成功");
    app::test::log_kv(TAG, "本机地址", ble.get_addr_str().c_str());
    app::test::log_section_end(TAG);

    app::test::log_section_begin(TAG, "[2] 注册 GATT 服务（设备信息 / 电池 / 配网）");
    if (!DevInfoSvc::get_instance().create("Ebook", "EP-TEST", "SN-0001", "0.1.0", "1.0", "0.1.0"))
    {
        app::test::log_kv(TAG, "DeviceInfoService", "失败：create");
        app::test::log_section_end(TAG);
        return;
    }
    app::test::log_kv(TAG, "DeviceInfoService", "成功");

    if (!BattSvc::get_instance().create())
    {
        app::test::log_kv(TAG, "BatteryService", "失败：create");
        app::test::log_section_end(TAG);
        return;
    }
    app::test::log_kv(TAG, "BatteryService", "成功");
    BattSvc::get_instance().update_level(100);

    auto& prov = ProvisionSvc::get_instance();
    prov.set_connect_cb([](const char* ssid, const char* password) {
        ESP_LOGI(TAG, "[配网] 收到连接 WiFi 请求： SSID=%s，密码长度=%u", ssid ? ssid : "",
                 static_cast<unsigned>(password ? strlen(password) : 0));
        ProvisionSvc::get_instance().update_status(PStatus::CONNECTING);
        ProvisionSvc::get_instance().update_status(PStatus::CONNECTED);
    });
    prov.set_disconnect_cb([]() {
        ESP_LOGI(TAG, "[配网] 收到断开请求");
        ProvisionSvc::get_instance().update_status(PStatus::IDLE);
    });
    prov.set_command_cb(
        [](PCmd cmd) { ESP_LOGI(TAG, "[配网] 收到命令： 0x%02x", static_cast<unsigned>(cmd)); });

    if (!prov.create())
    {
        app::test::log_kv(TAG, "ProvisionService", "失败：create");
        app::test::log_section_end(TAG);
        return;
    }
    app::test::log_kv(TAG, "ProvisionService", "成功");
    app::test::log_section_end(TAG);

    app::test::log_section_begin(TAG, "[3] 启动 GATT Server");
    if (!ble.start_server())
    {
        app::test::log_kv(TAG, "结果", "失败：start_server");
        app::test::log_section_end(TAG);
        return;
    }
    app::test::log_kv(TAG, "结果", "成功");
    app::test::log_section_end(TAG);

    app::test::log_section_begin(TAG, "[4] 开始可连接广播");
    app::network::bluetooth::AdvCfg adv;
    adv.device_name = BLE_TEST_DEVICE_NAME;
    if (!ble.start_advertising(adv, 0))
    {
        app::test::log_kv(TAG, "结果", "失败：start_advertising");
        app::test::log_section_end(TAG);
        return;
    }
    app::test::log_kv(TAG, "当前协议栈状态", state_str_zh(ble.get_state()));
    app::test::log_kv(TAG, "操作提示",
                      "使用手机 nRF Connect 等扫描并连接，可读设备信息 / 电量 / 配网特征");
    app::test::log_section_end(TAG);

    app::test::log_section_begin(TAG, "[5] 常驻运行（心跳与电量演示）");
    app::test::log_kv(TAG, "说明", "约每 8 秒打印心跳；电池电量通过 notify 递减演示，随后回满循环");
    app::test::log_section_end(TAG);

    uint8_t level = 100;
    for (;;)
    {
        app::sys::task::TaskMgr::delay_ms(8000);
        ESP_LOGI(TAG, "  · 心跳： 状态=%s，当前连接数=%u", state_str_zh(ble.get_state()),
                 static_cast<unsigned>(ble.get_conn_count()));
        level = (level > 15) ? static_cast<uint8_t>(level - 15) : 100;
        BattSvc::get_instance().update_level(level);
    }
}
