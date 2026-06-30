#include "bluetooth.hpp"

#include "NimBLEDevice.h"
#include "esp_log.h"

#include <algorithm>
#include <map>

static const char* const TAG = "Bluetooth";

namespace app::network::bluetooth {

static std::map<NimBLECharacteristic*, CharCbs> s_char_cbs;

class ServerCbs : public NimBLEServerCallbacks
{
  public:
    void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override
    {
        ConnInfo info;
        info.conn_handle = connInfo.getConnHandle();
        info.mtu = 23;
        info.encrypted = connInfo.isEncrypted();

        NimBLEAddress addr = connInfo.getAddress();
        const uint8_t* val = addr.getVal();
        if (val != nullptr)
        {
            memcpy(info.address, val, 6);
        }

        BleMgr::get_instance().on_connect(info);
    }

    void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override
    {
        ConnInfo info;
        info.conn_handle = connInfo.getConnHandle();

        NimBLEAddress addr = connInfo.getAddress();
        const uint8_t* val = addr.getVal();
        if (val != nullptr)
        {
            memcpy(info.address, val, 6);
        }

        BleMgr::get_instance().on_disconnect(info, reason);
    }

    void onMTUChange(uint16_t MTU, NimBLEConnInfo& connInfo) override
    {
        BleMgr::get_instance().on_mtu_change(MTU, connInfo.getConnHandle());
    }

    uint32_t onPassKeyDisplay() override
    {
        return 123456;
    }

    void onConfirmPassKey(NimBLEConnInfo& connInfo, uint32_t pin) override
    {
        NimBLEDevice::injectConfirmPasskey(connInfo, true);
    }

    void onAuthenticationComplete(NimBLEConnInfo& connInfo) override
    {
        if (!connInfo.isEncrypted())
        {
            ESP_LOGW(TAG, "认证失败");
        }
    }
};

class CharCbsImpl : public NimBLECharacteristicCallbacks
{
  public:
    void onRead(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override
    {
        auto it = s_char_cbs.find(pCharacteristic);
        if (it != s_char_cbs.end() && it->second.on_read)
        {
            ConnInfo info;
            info.conn_handle = connInfo.getConnHandle();
            info.encrypted = connInfo.isEncrypted();

            NimBLEAddress addr = connInfo.getAddress();
            const uint8_t* val = addr.getVal();
            if (val != nullptr)
            {
                memcpy(info.address, val, 6);
            }

            it->second.on_read(pCharacteristic, info);
        }
    }

    void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override
    {
        auto it = s_char_cbs.find(pCharacteristic);
        if (it != s_char_cbs.end() && it->second.on_write)
        {
            ConnInfo info;
            info.conn_handle = connInfo.getConnHandle();
            info.encrypted = connInfo.isEncrypted();

            NimBLEAddress addr = connInfo.getAddress();
            const uint8_t* addrVal = addr.getVal();
            if (addrVal != nullptr)
            {
                memcpy(info.address, addrVal, 6);
            }

            NimBLEAttValue chrVal = pCharacteristic->getValue();
            it->second.on_write(pCharacteristic, info, chrVal.data(), chrVal.size());
        }
    }

    void onSubscribe(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo,
                     uint16_t subValue) override
    {
        auto it = s_char_cbs.find(pCharacteristic);
        if (it != s_char_cbs.end() && it->second.on_subscribe)
        {
            ConnInfo info;
            info.conn_handle = connInfo.getConnHandle();
            info.encrypted = connInfo.isEncrypted();

            NimBLEAddress addr = connInfo.getAddress();
            const uint8_t* val = addr.getVal();
            if (val != nullptr)
            {
                memcpy(info.address, val, 6);
            }

            it->second.on_subscribe(pCharacteristic, info, subValue);
        }
    }
};

static ServerCbs s_server_cbs;
static CharCbsImpl s_char_cbs_impl;

BleMgr& BleMgr::get_instance()
{
    static BleMgr s;
    return s;
}

BleMgr::BleMgr()
    : init_(false)
    , state_(State::UNINIT)
    , server_(nullptr)
    , sec_mode_(SecMode::NONE)
    , passkey_(123456)
{
}

BleMgr::~BleMgr()
{
    deinit(true);
}

bool BleMgr::init(const char* device_name)
{
    std::lock_guard<std::mutex> lock(mtx_);

    if (init_)
    {
        ESP_LOGW(TAG, "已初始化");
        return true;
    }

    if (!NimBLEDevice::init(device_name ? device_name : "EmotiPet"))
    {
        ESP_LOGE(TAG, "初始化失败");
        return false;
    }

    adv_cfg_.device_name = device_name;
    init_ = true;
    state_ = State::IDLE;

    ESP_LOGI(TAG, "初始化完成: %s", device_name);
    return true;
}

void BleMgr::deinit(bool clear_all)
{
    std::lock_guard<std::mutex> lock(mtx_);

    if (!init_)
    {
        return;
    }

    stop_advertising();
    s_char_cbs.clear();

    NimBLEDevice::deinit(clear_all);

    server_ = nullptr;
    services_.clear();
    init_ = false;
    state_ = State::UNINIT;
    conns_.clear();

    ESP_LOGI(TAG, "反初始化完成");
}

NimBLEService* BleMgr::create_service(const char* uuid)
{
    std::lock_guard<std::mutex> lock(mtx_);

    if (!init_)
    {
        ESP_LOGE(TAG, "未初始化");
        return nullptr;
    }

    if (!server_)
    {
        server_ = NimBLEDevice::createServer();
        if (!server_)
        {
            ESP_LOGE(TAG, "创建服务器失败");
            return nullptr;
        }
        server_->setCallbacks(&s_server_cbs);
    }

    NimBLEService* svc = server_->createService(uuid);
    if (svc)
    {
        services_.push_back(svc);
    }
    return svc;
}

NimBLECharacteristic* BleMgr::create_characteristic(NimBLEService* service, const char* uuid,
                                                    uint32_t properties, uint16_t max_len)
{
    if (!service)
    {
        return nullptr;
    }

    return service->createCharacteristic(uuid, properties, max_len);
}

void BleMgr::set_char_cbs(NimBLECharacteristic* chr, const CharCbs& cbs)
{
    if (!chr)
    {
        return;
    }

    s_char_cbs[chr] = cbs;
    chr->setCallbacks(&s_char_cbs_impl);
}

bool BleMgr::start_service(NimBLEService* service)
{
    // 服务在 start_server() -> server_->start() 时一并启动
    return service != nullptr;
}

bool BleMgr::start_server()
{
    std::lock_guard<std::mutex> lock(mtx_);

    if (!init_ || !server_)
    {
        ESP_LOGE(TAG, "未初始化或服务器不存在");
        return false;
    }

    server_->start();
    ESP_LOGI(TAG, "服务器已启动，包含 %d 个服务", (int)services_.size());
    return true;
}

bool BleMgr::start_advertising(const AdvCfg& config, uint32_t duration_ms)
{
    StateCb cb;
    {
        std::lock_guard<std::mutex> lock(mtx_);

        if (!init_)
        {
            ESP_LOGE(TAG, "未初始化");
            return false;
        }

        if (state_ == State::ADVERTISING)
        {
            ESP_LOGW(TAG, "已在广播中");
            return true;
        }

        adv_cfg_ = config;

        NimBLEAdvertising* pAdv = NimBLEDevice::getAdvertising();
        if (!pAdv)
        {
            ESP_LOGE(TAG, "获取广播对象失败");
            return false;
        }

        pAdv->setName(config.device_name ? config.device_name : "EmotiPet");
        pAdv->setMinInterval(config.min_interval);
        pAdv->setMaxInterval(config.max_interval);
        pAdv->enableScanResponse(config.scan_response);

        if (config.appearance != 0)
        {
            pAdv->setAppearance(config.appearance);
        }

        for (auto* svc : services_)
        {
            pAdv->addServiceUUID(svc->getUUID());
        }

        if (!pAdv->start(duration_ms))
        {
            ESP_LOGE(TAG, "启动广播失败");
            return false;
        }

        state_ = State::ADVERTISING;
        cb = state_cb_;
    }

    if (cb)
    {
        cb(State::ADVERTISING);
    }

    ESP_LOGI(TAG, "广播已启动: %s", config.device_name);
    return true;
}

bool BleMgr::stop_advertising()
{
    StateCb cb;
    State new_state = State::IDLE;
    {
        std::lock_guard<std::mutex> lock(mtx_);

        if (!init_ || state_ != State::ADVERTISING)
        {
            return true;
        }

        if (!NimBLEDevice::stopAdvertising())
        {
            ESP_LOGE(TAG, "停止广播失败");
            return false;
        }

        new_state = conns_.empty() ? State::IDLE : State::CONNECTED;
        state_ = new_state;
        cb = state_cb_;
    }

    if (cb)
    {
        cb(new_state);
    }

    ESP_LOGI(TAG, "广播已停止");
    return true;
}

bool BleMgr::disconnect(uint16_t conn_handle, uint8_t reason)
{
    std::lock_guard<std::mutex> lock(mtx_);

    if (!init_ || !server_)
    {
        return false;
    }

    if (conn_handle == 0xFFFF)
    {
        auto peers = server_->getPeerDevices();
        for (auto handle : peers)
        {
            server_->disconnect(handle, reason);
        }
        return true;
    }

    return server_->disconnect(conn_handle, reason);
}

uint8_t BleMgr::get_conn_count() const
{
    std::lock_guard<std::mutex> lock(mtx_);

    if (!server_)
    {
        return 0;
    }
    return server_->getConnectedCount();
}

std::vector<ConnInfo> BleMgr::get_connected() const
{
    std::lock_guard<std::mutex> lock(mtx_);
    return conns_;
}

void BleMgr::set_state_cb(StateCb cb)
{
    std::lock_guard<std::mutex> lock(mtx_);
    state_cb_ = std::move(cb);
}

void BleMgr::set_connect_cb(ConnectCb cb)
{
    std::lock_guard<std::mutex> lock(mtx_);
    connect_cb_ = std::move(cb);
}

void BleMgr::set_disconnect_cb(DisconnectCb cb)
{
    std::lock_guard<std::mutex> lock(mtx_);
    disconnect_cb_ = std::move(cb);
}

void BleMgr::set_security(SecMode mode, uint32_t passkey)
{
    std::lock_guard<std::mutex> lock(mtx_);

    sec_mode_ = mode;
    passkey_ = passkey;

    if (!init_)
    {
        return;
    }

    switch (mode)
    {
        case SecMode::NONE:
            NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
            break;
        case SecMode::PASSKEY:
            NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
            NimBLEDevice::setSecurityPasskey(passkey);
            break;
        case SecMode::NUMERIC_COMP:
            NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_YESNO);
            break;
    }
}

bool BleMgr::set_mtu(uint16_t mtu)
{
    return NimBLEDevice::setMTU(mtu);
}

uint16_t BleMgr::get_mtu() const
{
    return NimBLEDevice::getMTU();
}

bool BleMgr::set_tx_power(int8_t dbm)
{
    return NimBLEDevice::setPower(dbm);
}

std::string BleMgr::get_addr_str() const
{
    return NimBLEDevice::getAddress().toString();
}

void BleMgr::on_connect(const ConnInfo& info)
{
    ConnectCb cb;
    StateCb state_cb;
    bool state_changed = false;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        conns_.push_back(info);
        cb = connect_cb_;

        if (state_ != State::CONNECTED)
        {
            state_ = State::CONNECTED;
            state_changed = true;
            state_cb = state_cb_;
        }
    }

    if (state_changed && state_cb)
    {
        state_cb(State::CONNECTED);
    }

    if (cb)
    {
        cb(info);
    }

    ESP_LOGI(TAG, "设备已连接: 句柄=%d", info.conn_handle);
}

void BleMgr::on_disconnect(const ConnInfo& info, int reason)
{
    DisconnectCb cb;
    StateCb state_cb;
    bool state_changed = false;
    State new_state = State::IDLE;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        conns_.erase(
            std::remove_if(conns_.begin(), conns_.end(),
                           [&](const ConnInfo& c) { return c.conn_handle == info.conn_handle; }),
            conns_.end());

        cb = disconnect_cb_;

        if (conns_.empty() && state_ != State::IDLE)
        {
            state_ = State::IDLE;
            state_changed = true;
            state_cb = state_cb_;
        }
    }

    if (state_changed && state_cb)
    {
        state_cb(new_state);
    }

    if (cb)
    {
        cb(info, reason);
    }

    ESP_LOGI(TAG, "设备已断开: 句柄=%d, 原因=%d", info.conn_handle, reason);
}

void BleMgr::on_mtu_change(uint16_t mtu, uint16_t conn_handle)
{
    std::lock_guard<std::mutex> lock(mtx_);

    for (auto& conn : conns_)
    {
        if (conn.conn_handle == conn_handle)
        {
            conn.mtu = mtu;
            break;
        }
    }

    ESP_LOGI(TAG, "MTU 已更新: 句柄=%d, MTU=%d", conn_handle, mtu);
}

void BleMgr::update_state(State new_state)
{
    StateCb cb;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (state_ == new_state)
        {
            return;
        }
        state_ = new_state;
        cb = state_cb_;
    }

    if (cb)
    {
        cb(new_state);
    }
}

} // namespace app::network::bluetooth
