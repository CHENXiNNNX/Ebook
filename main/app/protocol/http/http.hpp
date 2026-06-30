#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "esp_err.h"
#include "esp_http_client.h"

namespace app::protocol::http {

enum class HttpMethod
{
    GET,
    POST,
    PUT,
    DELETE,
    PATCH,
    HEAD,
    OPTIONS
};

enum class HttpStatus
{
    OK = 200,
    CREATED = 201,
    NO_CONTENT = 204,
    BAD_REQUEST = 400,
    UNAUTHORIZED = 401,
    FORBIDDEN = 403,
    NOT_FOUND = 404,
    INTERNAL_ERROR = 500,
    BAD_GATEWAY = 502,
    SERVICE_UNAVAILABLE = 503,
    UNKNOWN = 0
};

struct HttpRequest
{
    std::string url;
    HttpMethod method = HttpMethod::GET;
    std::map<std::string, std::string> headers;
    std::vector<uint8_t> body;
    int32_t timeout_ms = 5000;
    bool skip_cert_common_name_check = false;
    const char* cert_pem = nullptr;
};

struct HttpResponse
{
    HttpStatus status_code = HttpStatus::UNKNOWN;
    int32_t status_code_int = 0;
    std::map<std::string, std::string> headers;
    std::vector<uint8_t> body;
    int32_t content_length = 0;
};

using ResponseCallback = std::function<bool(const HttpResponse& response)>;
using DataCallback = std::function<bool(const uint8_t* data, size_t len)>;

/** @brief esp_http_client 封装（同步 perform + HTTPS crt_bundle） */
class HttpMgr
{
  public:
    static HttpMgr& get_instance();

    bool init();
    void deinit();

    bool perform(const HttpRequest& request, HttpResponse& response);
    bool perform(const HttpRequest& request, const ResponseCallback& response_callback,
                 const DataCallback& data_callback = nullptr);

    bool get(const std::string& url, HttpResponse& response, int32_t timeout_ms = 5000);
    bool post(const std::string& url, const std::vector<uint8_t>& body, HttpResponse& response,
              int32_t timeout_ms = 5000);
    bool post(const std::string& url, const std::string& body, HttpResponse& response,
              int32_t timeout_ms = 5000);

    bool is_init() const;

  private:
    bool perform_internal(const HttpRequest& request, HttpResponse* response,
                          const ResponseCallback& response_callback,
                          const DataCallback& data_callback);

    static esp_http_client_method_t convert_method(HttpMethod method);
    static HttpStatus convert_status_code(int32_t status_code_int);
    static void configure_client(const HttpRequest& request, esp_http_client_config_t& config);
    static esp_err_t setup_client(esp_http_client_handle_t client, const HttpRequest& request);
    static esp_err_t http_event_handler(esp_http_client_event_t* evt);

    HttpMgr() = default;
    ~HttpMgr();
    HttpMgr(const HttpMgr&) = delete;
    HttpMgr& operator=(const HttpMgr&) = delete;

    mutable std::mutex mutex_;
    bool initialized_{false};
    void* current_client_handle_{nullptr};
    HttpResponse* current_response_{nullptr};
    ResponseCallback current_response_callback_;
    DataCallback current_data_callback_;
};

} // namespace app::protocol::http
