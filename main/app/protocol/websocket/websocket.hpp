#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

#include "esp_err.h"
#include "esp_websocket_client.h"

namespace app::protocol::websocket {

enum class State
{
    IDLE,
    INITIALIZED,
    CONNECTING,
    CONNECTED,
    DISCONNECTED,
};

enum class EventType
{
    CONNECTED,
    DISCONNECTED,
    DATA,
    ERROR,
};

struct DataEvent
{
    const uint8_t* data;
    size_t length;
    bool is_text;
};

struct ErrorEvent
{
    esp_err_t esp_err_code;
    esp_websocket_error_type_t error_type;
    int handshake_status;
    std::string message;
};

using ConnectedCallback = std::function<void()>;
using DisconnectedCallback = std::function<void()>;
using DataCallback = std::function<void(const DataEvent& event)>;
using ErrorCallback = std::function<void(const ErrorEvent& event)>;
using StateCallback = std::function<void(State state)>;

struct Config
{
    std::string uri;
    std::string host;
    int port = 0;
    std::string path;
    std::string subprotocol;
    std::string headers;
    int ping_interval_sec = 10;
    int pingpong_timeout_sec = 10;
    int reconnect_timeout_ms = 10000;
    int network_timeout_ms = 10000;
    size_t buffer_size = 16384;
    bool disable_auto_reconnect = false;
    bool disable_pingpong_discon = false;
    const char* cert_pem = nullptr;
    size_t cert_len = 0;
    bool skip_cert_common_name_check = false;
    bool use_global_ca_store = false;
    bool skip_cert_verification = true;
};

/** @brief esp_websocket_client 单例封装 */
class WebSocketMgr
{
  public:
    static WebSocketMgr& get_instance();

    bool init(const Config& config);
    void deinit();

    bool connect();
    bool disconnect();

    int send_text(const std::string& text, int timeout_ms = 5000);
    int send_binary(const uint8_t* data, size_t len, int timeout_ms = 5000);

    State get_state() const;
    bool is_connected() const;
    bool is_init() const;

    void set_connected_callback(ConnectedCallback callback);
    void set_disconnected_callback(DisconnectedCallback callback);
    void set_data_callback(DataCallback callback);
    void set_error_callback(ErrorCallback callback);
    void set_state_callback(StateCallback callback);

  private:
    static void websocket_event_handler(void* handler_args, esp_event_base_t base, int32_t event_id,
                                        void* event_data);

    void handle_connected();
    void handle_disconnected();
    void handle_data(esp_websocket_event_data_t* event_data);
    void handle_error(esp_websocket_event_data_t* event_data);
    void set_state(State new_state);

    WebSocketMgr() = default;
    ~WebSocketMgr();
    WebSocketMgr(const WebSocketMgr&) = delete;
    WebSocketMgr& operator=(const WebSocketMgr&) = delete;

    mutable std::mutex mutex_;
    State state_ = State::IDLE;
    bool initialized_ = false;

    esp_websocket_client_handle_t client_handle_ = nullptr;
    Config config_;

    ConnectedCallback connected_callback_;
    DisconnectedCallback disconnected_callback_;
    DataCallback data_callback_;
    ErrorCallback error_callback_;
    StateCallback state_callback_;
};

} // namespace app::protocol::websocket
