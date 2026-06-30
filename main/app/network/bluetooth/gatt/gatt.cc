#include "gatt.hpp"

#include "NimBLEDevice.h"
#include "esp_log.h"

#include <cstring>

static const char* const TAG = "BLE_GATT";

namespace app::network::bluetooth::gatt {

ProvisionService& ProvisionService::get_instance()
{
    static ProvisionService instance;
    return instance;
}

ProvisionService::ProvisionService()
    : created_(false)
    , service_(nullptr)
    , ssid_char_(nullptr)
    , password_char_(nullptr)
    , status_char_(nullptr)
    , command_char_(nullptr)
    , status_(ProvisionStatus::IDLE)
{
}

bool ProvisionService::create()
{
    if (created_)
    {
        return true;
    }

    auto& mgr = BleMgr::get_instance();
    if (!mgr.is_init())
    {
        ESP_LOGE(TAG, "BLE 未初始化");
        return false;
    }

    service_ = mgr.create_service(UUID::PROVISION_SERVICE);
    if (!service_)
    {
        ESP_LOGE(TAG, "创建配网服务失败");
        return false;
    }

    ssid_char_ = mgr.create_characteristic(service_, UUID::WIFI_SSID_CHAR,
                                           NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::READ, 32);

    password_char_ =
        mgr.create_characteristic(service_, UUID::WIFI_PASSWORD_CHAR, NIMBLE_PROPERTY::WRITE, 64);

    status_char_ = mgr.create_characteristic(service_, UUID::WIFI_STATUS_CHAR,
                                             NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY, 32);

    command_char_ =
        mgr.create_characteristic(service_, UUID::WIFI_COMMAND_CHAR, NIMBLE_PROPERTY::WRITE, 16);

    if (!ssid_char_ || !password_char_ || !status_char_ || !command_char_)
    {
        ESP_LOGE(TAG, "创建特征失败");
        return false;
    }

    CharCbs ssid_cbs;
    ssid_cbs.on_write = [this](NimBLECharacteristic* chr, const ConnInfo& conn, const uint8_t* data,
                               size_t len) {
        on_ssid_write(chr, conn, data, len);
    };
    mgr.set_char_cbs(ssid_char_, ssid_cbs);

    CharCbs pwd_cbs;
    pwd_cbs.on_write = [this](NimBLECharacteristic* chr, const ConnInfo& conn, const uint8_t* data,
                              size_t len) {
        on_password_write(chr, conn, data, len);
    };
    mgr.set_char_cbs(password_char_, pwd_cbs);

    CharCbs cmd_cbs;
    cmd_cbs.on_write = [this](NimBLECharacteristic* chr, const ConnInfo& conn, const uint8_t* data,
                              size_t len) {
        on_command_write(chr, conn, data, len);
    };
    mgr.set_char_cbs(command_char_, cmd_cbs);

    uint8_t initial_status = static_cast<uint8_t>(ProvisionStatus::IDLE);
    status_char_->setValue(&initial_status, 1);

    mgr.start_service(service_);

    created_ = true;
    ESP_LOGI(TAG, "配网服务创建完成");
    return true;
}

void ProvisionService::set_connect_cb(ProvisionConnectCb cb)
{
    connect_cb_ = std::move(cb);
}

void ProvisionService::set_disconnect_cb(ProvisionDisconnectCb cb)
{
    disconnect_cb_ = std::move(cb);
}

void ProvisionService::set_command_cb(ProvisionCommandCb cb)
{
    command_cb_ = std::move(cb);
}

void ProvisionService::update_status(ProvisionStatus status)
{
    status_ = status;
    if (status_char_)
    {
        uint8_t val = static_cast<uint8_t>(status);
        status_char_->setValue(&val, 1);
        status_char_->notify();
    }
}

void ProvisionService::send_status_data(const uint8_t* data, size_t len)
{
    if (status_char_ && data != nullptr && len > 0)
    {
        status_char_->notify(data, len);
    }
}

void ProvisionService::on_ssid_write(NimBLECharacteristic* chr, const ConnInfo& conn,
                                     const uint8_t* data, size_t len)
{
    (void)chr;
    (void)conn;
    if (data != nullptr && len > 0 && len < 32)
    {
        ssid_.assign(reinterpret_cast<const char*>(data), len);
        ESP_LOGI(TAG, "SSID 已设置: %s", ssid_.c_str());
    }
}

void ProvisionService::on_password_write(NimBLECharacteristic* chr, const ConnInfo& conn,
                                         const uint8_t* data, size_t len)
{
    (void)chr;
    (void)conn;
    if (data != nullptr && len > 0 && len < 64)
    {
        password_.assign(reinterpret_cast<const char*>(data), len);
        ESP_LOGI(TAG, "密码已设置 len=%u", static_cast<unsigned>(len));
    }
}

void ProvisionService::on_command_write(NimBLECharacteristic* chr, const ConnInfo& conn,
                                        const uint8_t* data, size_t len)
{
    (void)chr;
    (void)conn;
    if (data == nullptr || len < 1)
    {
        return;
    }

    auto cmd = static_cast<ProvisionCommand>(data[0]);
    ESP_LOGI(TAG, "收到命令: 0x%02X", data[0]);

    switch (cmd)
    {
        case ProvisionCommand::CONNECT:
            if (!ssid_.empty() && connect_cb_)
            {
                connect_cb_(ssid_.c_str(), password_.c_str());
            }
            break;

        case ProvisionCommand::DISCONNECT:
            if (disconnect_cb_)
            {
                disconnect_cb_();
            }
            break;

        default:
            if (command_cb_)
            {
                command_cb_(cmd);
            }
            break;
    }
}

DeviceInfoService& DeviceInfoService::get_instance()
{
    static DeviceInfoService instance;
    return instance;
}

DeviceInfoService::DeviceInfoService()
    : created_(false)
    , service_(nullptr)
{
}

bool DeviceInfoService::create(const char* manufacturer, const char* model, const char* serial,
                               const char* firmware_rev, const char* hardware_rev,
                               const char* software_rev)
{
    if (created_)
    {
        return true;
    }

    auto& mgr = BleMgr::get_instance();
    if (!mgr.is_init())
    {
        ESP_LOGE(TAG, "BLE 未初始化");
        return false;
    }

    service_ = mgr.create_service(UUID::DEVICE_INFO_SERVICE);
    if (!service_)
    {
        ESP_LOGE(TAG, "创建设备信息服务失败");
        return false;
    }

    if (manufacturer != nullptr)
    {
        auto* chr =
            mgr.create_characteristic(service_, UUID::MANUFACTURER_CHAR, NIMBLE_PROPERTY::READ);
        if (chr != nullptr)
        {
            chr->setValue(manufacturer);
        }
    }

    if (model != nullptr)
    {
        auto* chr =
            mgr.create_characteristic(service_, UUID::MODEL_NUMBER_CHAR, NIMBLE_PROPERTY::READ);
        if (chr != nullptr)
        {
            chr->setValue(model);
        }
    }

    if (serial != nullptr)
    {
        auto* chr =
            mgr.create_characteristic(service_, UUID::SERIAL_NUMBER_CHAR, NIMBLE_PROPERTY::READ);
        if (chr != nullptr)
        {
            chr->setValue(serial);
        }
    }

    if (firmware_rev != nullptr)
    {
        auto* chr =
            mgr.create_characteristic(service_, UUID::FIRMWARE_REV_CHAR, NIMBLE_PROPERTY::READ);
        if (chr != nullptr)
        {
            chr->setValue(firmware_rev);
        }
    }

    if (hardware_rev != nullptr)
    {
        auto* chr =
            mgr.create_characteristic(service_, UUID::HARDWARE_REV_CHAR, NIMBLE_PROPERTY::READ);
        if (chr != nullptr)
        {
            chr->setValue(hardware_rev);
        }
    }

    if (software_rev != nullptr)
    {
        auto* chr =
            mgr.create_characteristic(service_, UUID::SOFTWARE_REV_CHAR, NIMBLE_PROPERTY::READ);
        if (chr != nullptr)
        {
            chr->setValue(software_rev);
        }
    }

    mgr.start_service(service_);

    created_ = true;
    ESP_LOGI(TAG, "设备信息服务创建完成");
    return true;
}

BatteryService& BatteryService::get_instance()
{
    static BatteryService instance;
    return instance;
}

BatteryService::BatteryService()
    : created_(false)
    , service_(nullptr)
    , level_char_(nullptr)
{
}

bool BatteryService::create()
{
    if (created_)
    {
        return true;
    }

    auto& mgr = BleMgr::get_instance();
    if (!mgr.is_init())
    {
        ESP_LOGE(TAG, "BLE 未初始化");
        return false;
    }

    service_ = mgr.create_service(UUID::BATTERY_SERVICE);
    if (!service_)
    {
        ESP_LOGE(TAG, "创建电池服务失败");
        return false;
    }

    level_char_ = mgr.create_characteristic(service_, UUID::BATTERY_LEVEL_CHAR,
                                            NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY, 1);

    if (!level_char_)
    {
        ESP_LOGE(TAG, "创建电池电量特征失败");
        return false;
    }

    uint8_t initial_level = 100;
    level_char_->setValue(&initial_level, 1);

    mgr.start_service(service_);

    created_ = true;
    ESP_LOGI(TAG, "电池服务创建完成");
    return true;
}

void BatteryService::update_level(uint8_t level)
{
    if (level > 100)
    {
        level = 100;
    }

    if (level_char_)
    {
        level_char_->setValue(&level, 1);
        level_char_->notify();
    }
}

} // namespace app::network::bluetooth::gatt
