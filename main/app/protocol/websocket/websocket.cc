#include "websocket.hpp"

#include <cstring>

#include "esp_log.h"
#include "esp_websocket_client.h"
#include "esp_crt_bundle.h"

static const char* const TAG = "WebSocket";

namespace app::protocol::websocket {

WebSocketMgr& WebSocketMgr::get_instance()
{
    static WebSocketMgr instance;
    return instance;
}

bool WebSocketMgr::init(const Config& config)
{
    StateCallback state_cb;
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (initialized_)
        {
            ESP_LOGW(TAG, "WebSocket 客户端已初始化");
            return false;
        }

        config_ = config;

        // 配置 WebSocket 客户端
        esp_websocket_client_config_t ws_cfg = {};

        if (!config.uri.empty())
        {
            ws_cfg.uri = config.uri.c_str();
        }
        else
        {
            if (!config.host.empty())
            {
                ws_cfg.host = config.host.c_str();
            }
            ws_cfg.port = config.port;
            if (!config.path.empty())
            {
                ws_cfg.path = config.path.c_str();
            }
        }

        if (!config.subprotocol.empty())
        {
            ws_cfg.subprotocol = config.subprotocol.c_str();
        }

        if (!config.headers.empty())
        {
            ws_cfg.headers = config.headers.c_str();
        }

        ws_cfg.ping_interval_sec = config.ping_interval_sec;
        ws_cfg.pingpong_timeout_sec = config.pingpong_timeout_sec;
        ws_cfg.reconnect_timeout_ms = config.reconnect_timeout_ms;
        ws_cfg.network_timeout_ms = config.network_timeout_ms;
        ws_cfg.buffer_size = config.buffer_size;
        ws_cfg.disable_auto_reconnect = config.disable_auto_reconnect;
        ws_cfg.disable_pingpong_discon = config.disable_pingpong_discon;
        ws_cfg.skip_cert_common_name_check = config.skip_cert_common_name_check;
        ws_cfg.use_global_ca_store = config.use_global_ca_store;

        // 配置 WSS/TLS 验证选项
        if (!config.uri.empty() && (config.uri.find("wss://") == 0))
        {
            if (config.cert_pem != nullptr && config.cert_len > 0)
            {
                // 使用自定义证书
                ws_cfg.cert_pem = config.cert_pem;
                ws_cfg.cert_len = config.cert_len;
                ESP_LOGI(TAG, "WSS 连接：使用自定义证书");
            }
            else if (config.skip_cert_verification)
            {
                // 跳过验证模式：挂载 bundle 但跳过名称检查
                // 这样可以绕过 "No server verification option set" 检查
                ws_cfg.crt_bundle_attach = esp_crt_bundle_attach;
                ws_cfg.skip_cert_common_name_check = true;
                ESP_LOGW(TAG, "WSS 连接：跳过证书验证（仅用于测试）");
            }
            else
            {
                // 默认模式：使用 ESP32 自带的 CA 证书包进行标准验证
                ws_cfg.crt_bundle_attach = esp_crt_bundle_attach;
                ESP_LOGI(TAG, "WSS 连接：使用系统证书包验证");
            }
        }

        // 自动判断传输类型（根据 URI 协议或端口）
        if (!config.uri.empty())
        {
            // 根据 URI 协议自动选择传输层
            ws_cfg.transport = (config.uri.find("wss://") == 0 || config.uri.find("https://") == 0)
                                   ? WEBSOCKET_TRANSPORT_OVER_SSL
                                   : WEBSOCKET_TRANSPORT_OVER_TCP;
        }
        else
        {
            // URI 为空时，根据端口判断（443 通常为 SSL）
            ws_cfg.transport =
                (config.port == 443) ? WEBSOCKET_TRANSPORT_OVER_SSL : WEBSOCKET_TRANSPORT_OVER_TCP;
        }

        // 设置事件处理器
        ws_cfg.user_context = this;

        // 初始化客户端
        client_handle_ = esp_websocket_client_init(&ws_cfg);
        if (client_handle_ == nullptr)
        {
            ESP_LOGE(TAG, "WebSocket 客户端初始化失败");
            return false;
        }

        // 注册事件处理器
        esp_err_t ret = esp_websocket_register_events(client_handle_, WEBSOCKET_EVENT_ANY,
                                                      websocket_event_handler, this);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "注册事件处理器失败: %s", esp_err_to_name(ret));
            esp_websocket_client_destroy(client_handle_);
            client_handle_ = nullptr;
            return false;
        }

        initialized_ = true;
        state_ = State::INITIALIZED;
        state_cb = state_callback_;
    }

    // 在锁外调用回调
    if (state_cb)
    {
        state_cb(State::INITIALIZED);
    }

    ESP_LOGI(TAG, "WebSocket 客户端初始化成功");
    return true;
}

WebSocketMgr::~WebSocketMgr()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_)
    {
        // 在析构函数中不调用 deinit()，因为它需要 unlock mutex
        // 直接清理资源
        if (state_ == State::CONNECTED || state_ == State::CONNECTING)
        {
            if (client_handle_ != nullptr)
            {
                esp_websocket_client_stop(client_handle_);
            }
        }

        if (client_handle_ != nullptr)
        {
            esp_websocket_client_destroy(client_handle_);
            client_handle_ = nullptr;
        }

        initialized_ = false;
        state_ = State::IDLE;
    }
}

void WebSocketMgr::deinit()
{
    bool should_stop = false;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!initialized_)
        {
            return;
        }

        // 标记需要停止连接
        if (state_ == State::CONNECTED || state_ == State::CONNECTING)
        {
            should_stop = true;
        }
    }

    // 在锁外停止连接（避免死锁）
    if (should_stop && client_handle_ != nullptr)
    {
        esp_websocket_client_stop(client_handle_);
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (client_handle_ != nullptr)
        {
            esp_websocket_client_destroy(client_handle_);
            client_handle_ = nullptr;
        }

        initialized_ = false;
        state_ = State::IDLE;

        // 清空回调
        connected_callback_ = nullptr;
        disconnected_callback_ = nullptr;
        data_callback_ = nullptr;
        error_callback_ = nullptr;
        state_callback_ = nullptr;
    }

    ESP_LOGI(TAG, "WebSocket 客户端已反初始化");
}

bool WebSocketMgr::connect()
{
    StateCallback state_cb;
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!initialized_)
        {
            ESP_LOGE(TAG, "WebSocket 客户端未初始化");
            return false;
        }

        if (state_ == State::CONNECTED || state_ == State::CONNECTING)
        {
            ESP_LOGW(TAG, "WebSocket 已连接或正在连接中");
            return false;
        }

        if (client_handle_ == nullptr)
        {
            ESP_LOGE(TAG, "WebSocket 客户端句柄无效");
            return false;
        }

        // 设置状态（在锁内）
        if (state_ != State::CONNECTING)
        {
            state_ = State::CONNECTING;
            state_cb = state_callback_;
        }
    }

    // 在锁外调用状态回调
    if (state_cb)
    {
        state_cb(State::CONNECTING);
    }

    esp_err_t ret = esp_websocket_client_start(client_handle_);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "启动 WebSocket 连接失败: %s", esp_err_to_name(ret));
        // 设置失败状态
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (state_ == State::CONNECTING)
            {
                state_ = State::DISCONNECTED;
                state_cb = state_callback_;
            }
            else
            {
                state_cb = nullptr;
            }
        }
        if (state_cb)
        {
            state_cb(State::DISCONNECTED);
        }
        return false;
    }

    ESP_LOGI(TAG, "正在连接 WebSocket 服务器...");
    return true;
}

bool WebSocketMgr::disconnect()
{
    StateCallback state_cb;
    bool should_stop = false;
    esp_websocket_client_handle_t handle_to_stop = nullptr;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!initialized_ || client_handle_ == nullptr)
        {
            return false;
        }

        if (state_ != State::CONNECTED && state_ != State::CONNECTING)
        {
            return false;
        }

        should_stop = true;
        handle_to_stop = client_handle_;
        state_cb = state_callback_;
    }

    // 在锁外调用 esp_websocket_client_stop（避免死锁）
    if (should_stop && handle_to_stop != nullptr)
    {
        esp_err_t ret = esp_websocket_client_stop(handle_to_stop);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "断开 WebSocket 连接失败: %s", esp_err_to_name(ret));
            return false;
        }

        // 更新状态（在锁内）
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (state_ != State::DISCONNECTED)
            {
                state_ = State::DISCONNECTED;
                state_cb = state_callback_;
            }
            else
            {
                state_cb = nullptr;
            }
        }

        // 在锁外调用状态回调
        if (state_cb)
        {
            state_cb(State::DISCONNECTED);
        }

        ESP_LOGI(TAG, "WebSocket 已断开连接");
        return true;
    }

    return false;
}

int WebSocketMgr::send_text(const std::string& text, int timeout_ms)
{
    esp_websocket_client_handle_t handle = nullptr;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!initialized_ || client_handle_ == nullptr)
        {
            ESP_LOGE(TAG, "WebSocket 客户端未初始化");
            return -1;
        }

        if (state_ != State::CONNECTED)
        {
            ESP_LOGE(TAG, "WebSocket 未连接，无法发送数据");
            return -1;
        }

        handle = client_handle_;
    }

    // 在锁外发送数据（避免长时间持锁）
    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    int sent = esp_websocket_client_send_text(handle, text.c_str(), static_cast<int>(text.length()),
                                              timeout_ticks);

    if (sent < 0)
    {
        ESP_LOGE(TAG, "发送文本消息失败");
    }
    else
    {
        ESP_LOGD(TAG, "已发送文本消息，长度: %d", sent);
    }

    return sent;
}

int WebSocketMgr::send_binary(const uint8_t* data, size_t len, int timeout_ms)
{
    esp_websocket_client_handle_t handle = nullptr;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!initialized_ || client_handle_ == nullptr)
        {
            ESP_LOGE(TAG, "WebSocket 客户端未初始化");
            return -1;
        }

        if (state_ != State::CONNECTED)
        {
            ESP_LOGE(TAG, "WebSocket 未连接，无法发送数据");
            return -1;
        }

        handle = client_handle_;
    }

    // 在锁外发送数据（避免长时间持锁，特别是大数据包）
    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    int sent = esp_websocket_client_send_bin(handle, reinterpret_cast<const char*>(data),
                                             static_cast<int>(len), timeout_ticks);

    if (sent < 0)
    {
        ESP_LOGE(TAG, "发送二进制数据失败");
    }
    else
    {
        ESP_LOGD(TAG, "已发送二进制数据，长度: %d", sent);
    }

    return sent;
}

State WebSocketMgr::get_state() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

bool WebSocketMgr::is_connected() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return state_ == State::CONNECTED;
}

bool WebSocketMgr::is_init() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return initialized_;
}

void WebSocketMgr::set_connected_callback(ConnectedCallback callback)
{
    std::lock_guard<std::mutex> lock(mutex_);
    connected_callback_ = callback;
}

void WebSocketMgr::set_disconnected_callback(DisconnectedCallback callback)
{
    std::lock_guard<std::mutex> lock(mutex_);
    disconnected_callback_ = callback;
}

void WebSocketMgr::set_data_callback(DataCallback callback)
{
    std::lock_guard<std::mutex> lock(mutex_);
    data_callback_ = callback;
}

void WebSocketMgr::set_error_callback(ErrorCallback callback)
{
    std::lock_guard<std::mutex> lock(mutex_);
    error_callback_ = callback;
}

void WebSocketMgr::set_state_callback(StateCallback callback)
{
    std::lock_guard<std::mutex> lock(mutex_);
    state_callback_ = callback;
}

void WebSocketMgr::websocket_event_handler(void* handler_args, esp_event_base_t base,
                                           int32_t event_id, void* event_data)
{
    auto* client = static_cast<WebSocketMgr*>(handler_args);
    if (client == nullptr)
    {
        return;
    }

    esp_websocket_event_id_t ws_event_id = static_cast<esp_websocket_event_id_t>(event_id);
    auto* event_data_ptr = static_cast<esp_websocket_event_data_t*>(event_data);

    switch (ws_event_id)
    {
        case WEBSOCKET_EVENT_CONNECTED:
            client->handle_connected();
            break;

        case WEBSOCKET_EVENT_DISCONNECTED:
            client->handle_disconnected();
            break;

        case WEBSOCKET_EVENT_DATA:
            client->handle_data(event_data_ptr);
            break;

        case WEBSOCKET_EVENT_ERROR:
            client->handle_error(event_data_ptr);
            break;

        case WEBSOCKET_EVENT_CLOSED:
            client->handle_disconnected();
            break;

        default:
            break;
    }
}

void WebSocketMgr::handle_connected()
{
    ConnectedCallback cb;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cb = connected_callback_;
    }

    ESP_LOGI(TAG, "WebSocket 已连接");

    set_state(State::CONNECTED);

    if (cb)
    {
        cb();
    }
}

void WebSocketMgr::handle_disconnected()
{
    DisconnectedCallback cb;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cb = disconnected_callback_;
    }

    ESP_LOGI(TAG, "WebSocket 已断开连接");

    set_state(State::DISCONNECTED);

    if (cb)
    {
        cb();
    }
}

void WebSocketMgr::handle_data(esp_websocket_event_data_t* event_data)
{
    if (event_data == nullptr)
    {
        return;
    }

    // 跳过长度为0的数据
    if (event_data->data_len == 0)
    {
        return;
    }

    DataCallback cb;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cb = data_callback_;
    }

    if (cb)
    {
        DataEvent event;
        event.data = reinterpret_cast<const uint8_t*>(event_data->data_ptr);
        event.length = static_cast<size_t>(event_data->data_len);
        event.is_text = (event_data->op_code == 0x1); // 文本帧 opcode = 0x1

        ESP_LOGD(TAG, "recv len=%u %s", static_cast<unsigned>(event.length),
                 event.is_text ? "text" : "bin");

        cb(event);
    }
}

void WebSocketMgr::handle_error(esp_websocket_event_data_t* event_data)
{
    if (event_data == nullptr)
    {
        return;
    }

    ErrorCallback cb;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cb = error_callback_;
    }

    if (cb)
    {
        ErrorEvent event;
        event.esp_err_code = event_data->error_handle.esp_tls_last_esp_err;
        event.error_type = event_data->error_handle.error_type;
        event.handshake_status = event_data->error_handle.esp_ws_handshake_status_code;

        // 生成错误消息
        char msg[128];
        snprintf(msg, sizeof(msg), "WebSocket 错误: type=%d, esp_err=0x%x, handshake=%d",
                 event.error_type, event.esp_err_code, event.handshake_status);
        event.message = msg;

        ESP_LOGE(TAG, "%s", event.message.c_str());

        cb(event);
    }
}

void WebSocketMgr::set_state(State new_state)
{
    StateCallback state_cb;
    bool state_changed = false;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ != new_state)
        {
            state_ = new_state;
            state_changed = true;
            state_cb = state_callback_;
        }
    }

    if (state_changed && state_cb)
    {
        state_cb(new_state);
    }
}

} // namespace app::protocol::websocket
