#include "http.hpp"

#include <cstring>

#include "esp_crt_bundle.h"
#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_log.h"

static const char* const TAG = "HTTP";

namespace app::protocol::http {

HttpMgr& HttpMgr::get_instance()
{
    static HttpMgr instance;
    return instance;
}

bool HttpMgr::init()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_)
    {
        return true;
    }

    current_client_handle_ = nullptr;
    current_response_ = nullptr;
    current_response_callback_ = nullptr;
    current_data_callback_ = nullptr;
    initialized_ = true;
    return true;
}

HttpMgr::~HttpMgr()
{
    if (initialized_)
    {
        deinit();
    }
}

void HttpMgr::deinit()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_)
    {
        return;
    }

    initialized_ = false;
    current_client_handle_ = nullptr;
    current_response_ = nullptr;
    current_response_callback_ = nullptr;
    current_data_callback_ = nullptr;
}

esp_err_t HttpMgr::http_event_handler(esp_http_client_event_t* evt)
{
    auto& instance = get_instance();

    switch (evt->event_id)
    {
        case HTTP_EVENT_ERROR:
            ESP_LOGE(TAG, "HTTP 事件错误");
            if (instance.current_response_)
            {
                instance.current_response_->status_code = HttpStatus::UNKNOWN;
            }
            break;

        case HTTP_EVENT_ON_HEADER:
            if (instance.current_response_ && evt->header_key && evt->header_value)
            {
                instance.current_response_->headers[evt->header_key] = evt->header_value;
            }
            break;

        case HTTP_EVENT_ON_DATA:
            if (instance.current_data_callback_)
            {
                if (!instance.current_data_callback_(static_cast<const uint8_t*>(evt->data),
                                                     static_cast<size_t>(evt->data_len)))
                {
                    return ESP_FAIL;
                }
            }

            if (instance.current_response_)
            {
                size_t data_len = static_cast<size_t>(evt->data_len);
                instance.current_response_->body.insert(
                    instance.current_response_->body.end(), static_cast<const uint8_t*>(evt->data),
                    static_cast<const uint8_t*>(evt->data) + data_len);
            }
            break;

        default:
            break;
    }

    return ESP_OK;
}

esp_http_client_method_t HttpMgr::convert_method(HttpMethod method)
{
    switch (method)
    {
        case HttpMethod::GET:
            return HTTP_METHOD_GET;
        case HttpMethod::POST:
            return HTTP_METHOD_POST;
        case HttpMethod::PUT:
            return HTTP_METHOD_PUT;
        case HttpMethod::DELETE:
            return HTTP_METHOD_DELETE;
        case HttpMethod::PATCH:
            return HTTP_METHOD_PATCH;
        case HttpMethod::HEAD:
            return HTTP_METHOD_HEAD;
        case HttpMethod::OPTIONS:
            return HTTP_METHOD_OPTIONS;
        default:
            return HTTP_METHOD_GET;
    }
}

HttpStatus HttpMgr::convert_status_code(int32_t status_code_int)
{
    switch (status_code_int)
    {
        case 200:
            return HttpStatus::OK;
        case 201:
            return HttpStatus::CREATED;
        case 204:
            return HttpStatus::NO_CONTENT;
        case 400:
            return HttpStatus::BAD_REQUEST;
        case 401:
            return HttpStatus::UNAUTHORIZED;
        case 403:
            return HttpStatus::FORBIDDEN;
        case 404:
            return HttpStatus::NOT_FOUND;
        case 500:
            return HttpStatus::INTERNAL_ERROR;
        case 502:
            return HttpStatus::BAD_GATEWAY;
        case 503:
            return HttpStatus::SERVICE_UNAVAILABLE;
        default:
            if (status_code_int >= 200 && status_code_int < 300)
            {
                return HttpStatus::OK;
            }
            return HttpStatus::UNKNOWN;
    }
}

void HttpMgr::configure_client(const HttpRequest& request, esp_http_client_config_t& config)
{
    config.url = request.url.c_str();
    config.event_handler = http_event_handler;
    config.timeout_ms = request.timeout_ms;
    config.skip_cert_common_name_check = request.skip_cert_common_name_check;
    config.cert_pem = request.cert_pem;

    // HTTPS 必须配置服务端校验，否则 esp-tls 报 ESP_ERR_MBEDTLS_SSL_SETUP_FAILED
    if (request.url.size() >= 8 && request.url.compare(0, 8, "https://") == 0)
    {
        if (request.cert_pem != nullptr)
        {
            return;
        }
        config.crt_bundle_attach = esp_crt_bundle_attach;
    }
}

esp_err_t HttpMgr::setup_client(esp_http_client_handle_t client, const HttpRequest& request)
{
    esp_http_client_method_t method = convert_method(request.method);
    esp_err_t ret = esp_http_client_set_method(client, method);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "设置 HTTP 方法失败: %s", esp_err_to_name(ret));
        return ret;
    }

    for (const auto& header : request.headers)
    {
        esp_http_client_set_header(client, header.first.c_str(), header.second.c_str());
    }

    if (!request.body.empty())
    {
        ret = esp_http_client_set_post_field(client, (const char*)request.body.data(),
                                             request.body.size());
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "设置请求体失败: %s", esp_err_to_name(ret));
            return ret;
        }
    }

    return ESP_OK;
}

bool HttpMgr::perform_internal(const HttpRequest& request, HttpResponse* response,
                               const ResponseCallback& response_callback,
                               const DataCallback& data_callback)
{
    if (request.url.empty())
    {
        ESP_LOGE(TAG, "URL 不能为空");
        return false;
    }

    if (response == nullptr && !response_callback)
    {
        ESP_LOGE(TAG, "必须提供 response 或 response_callback");
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_)
        {
            ESP_LOGE(TAG, "HTTP 客户端未初始化");
            return false;
        }
    }

    if (response)
    {
        response->status_code = HttpStatus::UNKNOWN;
        response->status_code_int = 0;
        response->headers.clear();
        response->body.clear();
        response->content_length = 0;
    }

    esp_http_client_config_t config = {};
    configure_client(request, config);

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr)
    {
        ESP_LOGE(TAG, "HTTP 客户端初始化失败");
        return false;
    }

    esp_err_t ret = setup_client(client, request);
    if (ret != ESP_OK)
    {
        esp_http_client_cleanup(client);
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        current_client_handle_ = client;
        current_response_ = response;
        current_response_callback_ = response_callback;
        current_data_callback_ = data_callback;
    }

    ret = esp_http_client_perform(client);
    bool success = (ret == ESP_OK);

    int32_t status_code_int = 0;
    int32_t content_length = 0;
    if (success)
    {
        status_code_int = esp_http_client_get_status_code(client);
        content_length = esp_http_client_get_content_length(client);
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        current_client_handle_ = nullptr;
        current_response_ = nullptr;
        current_response_callback_ = nullptr;
        current_data_callback_ = nullptr;
    }

    if (success)
    {
        HttpStatus status_code = convert_status_code(status_code_int);

        if (response)
        {
            response->status_code = status_code;
            response->status_code_int = status_code_int;
            response->content_length = content_length;
        }

        if (response_callback)
        {
            HttpResponse callback_response;
            callback_response.status_code = status_code;
            callback_response.status_code_int = status_code_int;
            callback_response.content_length = content_length;

            if (!response_callback(callback_response))
            {
                success = false;
            }
        }
    }
    else
    {
        ESP_LOGE(TAG, "HTTP 请求失败: %s, 错误: %s", request.url.c_str(), esp_err_to_name(ret));
    }

    esp_http_client_cleanup(client);
    return success;
}

bool HttpMgr::perform(const HttpRequest& request, HttpResponse& response)
{
    return perform_internal(request, &response, nullptr, nullptr);
}

bool HttpMgr::perform(const HttpRequest& request, const ResponseCallback& response_callback,
                      const DataCallback& data_callback)
{
    return perform_internal(request, nullptr, response_callback, data_callback);
}

bool HttpMgr::get(const std::string& url, HttpResponse& response, int32_t timeout_ms)
{
    HttpRequest request;
    request.url = url;
    request.method = HttpMethod::GET;
    request.timeout_ms = timeout_ms;

    return perform(request, response);
}

bool HttpMgr::post(const std::string& url, const std::vector<uint8_t>& body, HttpResponse& response,
                   int32_t timeout_ms)
{
    HttpRequest request;
    request.url = url;
    request.method = HttpMethod::POST;
    request.body = body;
    request.timeout_ms = timeout_ms;
    request.headers["Content-Type"] = "application/octet-stream";
    return perform(request, response);
}

bool HttpMgr::post(const std::string& url, const std::string& body, HttpResponse& response,
                   int32_t timeout_ms)
{
    HttpRequest request;
    request.url = url;
    request.method = HttpMethod::POST;
    request.timeout_ms = timeout_ms;
    request.body.assign(body.begin(), body.end());
    request.headers["Content-Type"] = "application/json";
    return perform(request, response);
}

bool HttpMgr::is_init() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return initialized_;
}

} // namespace app::protocol::http
