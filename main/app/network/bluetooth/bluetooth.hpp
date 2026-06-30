#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

class NimBLEServer;
class NimBLEService;
class NimBLECharacteristic;

namespace app::network::bluetooth {

enum class State
{
    UNINIT,
    IDLE,
    ADVERTISING,
    CONNECTED,
};

enum class SecMode
{
    NONE,
    PASSKEY,
    NUMERIC_COMP,
};

struct ConnInfo
{
    uint16_t conn_handle;
    uint8_t address[6];
    uint16_t mtu;
    bool encrypted;

    ConnInfo()
        : conn_handle(0)
        , address{}
        , mtu(23)
        , encrypted(false)
    {
    }
};

struct AdvCfg
{
    const char* device_name;
    uint16_t min_interval;
    uint16_t max_interval;
    bool connectable;
    bool scan_response;
    uint16_t appearance;

    AdvCfg()
        : device_name("EmotiPet")
        , min_interval(160)
        , max_interval(320)
        , connectable(true)
        , scan_response(true)
        , appearance(0)
    {
    }
};

namespace Prop {

constexpr uint32_t READ = 0x0001;
constexpr uint32_t WRITE = 0x0002;
constexpr uint32_t WRITE_NR = 0x0004;
constexpr uint32_t NOTIFY = 0x0008;
constexpr uint32_t INDICATE = 0x0010;
constexpr uint32_t READ_ENC = 0x0100;
constexpr uint32_t WRITE_ENC = 0x0200;
constexpr uint32_t READ_AUTHEN = 0x0400;
constexpr uint32_t WRITE_AUTHEN = 0x0800;

} // namespace Prop

using StateCb = std::function<void(State state)>;
using ConnectCb = std::function<void(const ConnInfo& info)>;
using DisconnectCb = std::function<void(const ConnInfo& info, int reason)>;
using CharReadCb = std::function<void(NimBLECharacteristic* chr, const ConnInfo& conn)>;
using CharWriteCb = std::function<void(NimBLECharacteristic* chr, const ConnInfo& conn,
                                       const uint8_t* data, size_t len)>;
using CharSubscribeCb =
    std::function<void(NimBLECharacteristic* chr, const ConnInfo& conn, uint16_t sub_value)>;

struct CharCbs
{
    CharReadCb on_read;
    CharWriteCb on_write;
    CharSubscribeCb on_subscribe;
};

/** @brief NimBLE 服务器封装（广播、连接、GATT 特征回调） */
class BleMgr
{
  public:
    static BleMgr& get_instance();

    bool init(const char* device_name = "EmotiPet");
    void deinit(bool clear_all = false);

    bool is_init() const { return init_; }

    NimBLEService* create_service(const char* uuid);

    NimBLECharacteristic* create_characteristic(NimBLEService* service, const char* uuid,
                                                uint32_t properties, uint16_t max_len = 512);

    void set_char_cbs(NimBLECharacteristic* chr, const CharCbs& cbs);

    bool start_service(NimBLEService* service);

    bool start_server();

    bool start_advertising(const AdvCfg& config = AdvCfg(), uint32_t duration_ms = 0);

    bool stop_advertising();

    bool disconnect(uint16_t conn_handle = 0xFFFF, uint8_t reason = 0x13);

    State get_state() const { return state_; }

    bool is_connected() const { return state_ == State::CONNECTED; }

    bool is_advertising() const { return state_ == State::ADVERTISING; }

    uint8_t get_conn_count() const;
    std::vector<ConnInfo> get_connected() const;

    void set_state_cb(StateCb cb);
    void set_connect_cb(ConnectCb cb);
    void set_disconnect_cb(DisconnectCb cb);

    void set_security(SecMode mode, uint32_t passkey = 123456);

    bool set_mtu(uint16_t mtu);
    uint16_t get_mtu() const;

    bool set_tx_power(int8_t dbm);

    std::string get_addr_str() const;

    NimBLEServer* get_server() const { return server_; }

    void on_connect(const ConnInfo& info);
    void on_disconnect(const ConnInfo& info, int reason);
    void on_mtu_change(uint16_t mtu, uint16_t conn_handle);

  private:
    BleMgr();
    ~BleMgr();
    BleMgr(const BleMgr&) = delete;
    BleMgr& operator=(const BleMgr&) = delete;

    void update_state(State new_state);

    mutable std::mutex mtx_;
    bool init_;
    State state_;
    NimBLEServer* server_;
    std::vector<NimBLEService*> services_;
    StateCb state_cb_;
    ConnectCb connect_cb_;
    DisconnectCb disconnect_cb_;
    SecMode sec_mode_;
    uint32_t passkey_;
    AdvCfg adv_cfg_;
    std::vector<ConnInfo> conns_;
};

} // namespace app::network::bluetooth
