#pragma once

#include <functional>
#include <string>

#include "bluetooth.hpp"

class NimBLEService;
class NimBLECharacteristic;

namespace app::network::bluetooth::gatt {

namespace UUID {

constexpr const char* GAP_SERVICE = "1800";
constexpr const char* GATT_SERVICE = "1801";
constexpr const char* DEVICE_INFO_SERVICE = "180A";
constexpr const char* BATTERY_SERVICE = "180F";

constexpr const char* DEVICE_NAME_CHAR = "2A00";
constexpr const char* APPEARANCE_CHAR = "2A01";
constexpr const char* BATTERY_LEVEL_CHAR = "2A19";
constexpr const char* MANUFACTURER_CHAR = "2A29";
constexpr const char* MODEL_NUMBER_CHAR = "2A24";
constexpr const char* SERIAL_NUMBER_CHAR = "2A25";
constexpr const char* FIRMWARE_REV_CHAR = "2A26";
constexpr const char* HARDWARE_REV_CHAR = "2A27";
constexpr const char* SOFTWARE_REV_CHAR = "2A28";

constexpr const char* PROVISION_SERVICE = "12345678-1234-5678-1234-56789abcdef0";
constexpr const char* WIFI_SSID_CHAR = "12345678-1234-5678-1234-56789abcdef1";
constexpr const char* WIFI_PASSWORD_CHAR = "12345678-1234-5678-1234-56789abcdef2";
constexpr const char* WIFI_STATUS_CHAR = "12345678-1234-5678-1234-56789abcdef3";
constexpr const char* WIFI_COMMAND_CHAR = "12345678-1234-5678-1234-56789abcdef4";

constexpr const char* DEVICE_CONTROL_SERVICE = "87654321-4321-8765-4321-fedcba987650";
constexpr const char* DEVICE_STATE_CHAR = "87654321-4321-8765-4321-fedcba987651";
constexpr const char* DEVICE_COMMAND_CHAR = "87654321-4321-8765-4321-fedcba987652";
constexpr const char* DEVICE_DATA_CHAR = "87654321-4321-8765-4321-fedcba987653";

} // namespace UUID

enum class ProvisionStatus : uint8_t
{
    IDLE = 0x00,
    CONNECTING = 0x01,
    CONNECTED = 0x02,
    FAILED_TIMEOUT = 0x10,
    FAILED_WRONG_PWD = 0x11,
    FAILED_NOT_FOUND = 0x12,
    FAILED_UNKNOWN = 0x1F,
};

enum class ProvisionCommand : uint8_t
{
    CONNECT = 0x01,
    DISCONNECT = 0x02,
    SCAN = 0x03,
    SAVE = 0x04,
    CLEAR = 0x05,
    GET_STATUS = 0x10,
    GET_IP = 0x11,
};

using ProvisionConnectCb = std::function<void(const char* ssid, const char* password)>;
using ProvisionDisconnectCb = std::function<void()>;
using ProvisionCommandCb = std::function<void(ProvisionCommand cmd)>;

/** WiFi 配网 GATT；须在 BleMgr::start_server() 之前 create() */
class ProvisionService
{
  public:
    static ProvisionService& get_instance();

    bool create();

    bool is_created() const { return created_; }

    void set_connect_cb(ProvisionConnectCb cb);
    void set_disconnect_cb(ProvisionDisconnectCb cb);
    void set_command_cb(ProvisionCommandCb cb);

    void update_status(ProvisionStatus status);

    void send_status_data(const uint8_t* data, size_t len);

    const std::string& get_ssid() const { return ssid_; }
    const std::string& get_password() const { return password_; }

  private:
    ProvisionService();
    ~ProvisionService() = default;
    ProvisionService(const ProvisionService&) = delete;
    ProvisionService& operator=(const ProvisionService&) = delete;

    void on_ssid_write(NimBLECharacteristic* chr, const ConnInfo& conn, const uint8_t* data,
                       size_t len);
    void on_password_write(NimBLECharacteristic* chr, const ConnInfo& conn, const uint8_t* data,
                           size_t len);
    void on_command_write(NimBLECharacteristic* chr, const ConnInfo& conn, const uint8_t* data,
                          size_t len);

    bool created_;
    NimBLEService* service_;
    NimBLECharacteristic* ssid_char_;
    NimBLECharacteristic* password_char_;
    NimBLECharacteristic* status_char_;
    NimBLECharacteristic* command_char_;
    std::string ssid_;
    std::string password_;
    ProvisionStatus status_;
    ProvisionConnectCb connect_cb_;
    ProvisionDisconnectCb disconnect_cb_;
    ProvisionCommandCb command_cb_;
};

/** 标准设备信息服务 0x180A */
class DeviceInfoService
{
  public:
    static DeviceInfoService& get_instance();

    bool create(const char* manufacturer = "EmotiPet", const char* model = "EP-001",
                const char* serial = "000001", const char* firmware_rev = "1.0.0",
                const char* hardware_rev = "1.0", const char* software_rev = "1.0.0");

    bool is_created() const { return created_; }

  private:
    DeviceInfoService();
    ~DeviceInfoService() = default;
    DeviceInfoService(const DeviceInfoService&) = delete;
    DeviceInfoService& operator=(const DeviceInfoService&) = delete;

    bool created_;
    NimBLEService* service_;
};

/** 标准电池服务 0x180F */
class BatteryService
{
  public:
    static BatteryService& get_instance();

    bool create();

    bool is_created() const { return created_; }

    void update_level(uint8_t level);

  private:
    BatteryService();
    ~BatteryService() = default;
    BatteryService(const BatteryService&) = delete;
    BatteryService& operator=(const BatteryService&) = delete;

    bool created_;
    NimBLEService* service_;
    NimBLECharacteristic* level_char_;
};

} // namespace app::network::bluetooth::gatt
