#include "ota.hpp"

#include <algorithm>
#include <cstring>
#include <ctime>
#include <memory>
#include <mutex>

#include "protocol/http/http.hpp"
#include "protocol/ntp/ntp.hpp"
#include "common/time/time.hpp"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "system/task/task.hpp"

static const char* const TAG = "OTA";

namespace app::common::ota {

OtaMgr& OtaMgr::get_instance()
{
    static OtaMgr instance;
    return instance;
}

bool OtaMgr::init(const std::string& device_id, const std::string& current_version)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_)
    {
        return true;
    }

    device_id_ = device_id;
    current_version_ = current_version;
    status_ = OtaStatus::IDLE;
    progress_callback_ = nullptr;
    status_callback_ = nullptr;
    complete_callback_ = nullptr;
    current_update_task_ = nullptr;
    cancelled_ = false;
    initialized_ = true;
    return true;
}

void OtaMgr::deinit()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_)
    {
        return;
    }

    if (status_ == OtaStatus::DOWNLOADING || status_ == OtaStatus::VERIFYING ||
        status_ == OtaStatus::CHECKING)
    {
        ESP_LOGW(TAG, "OTA 升级进行中，无法去初始化");
        return;
    }

    initialized_ = false;
    status_ = OtaStatus::IDLE;
    progress_callback_ = nullptr;
    status_callback_ = nullptr;
    complete_callback_ = nullptr;
    current_update_task_ = nullptr;
    cancelled_ = false;
}

std::string OtaMgr::get_timestamp() const
{
    auto& ntp_mgr = app::protocol::ntp::NtpMgr::get_instance();
    if (ntp_mgr.is_init() && ntp_mgr.get_sync_status() != app::protocol::ntp::SyncStatus::COMPLETED)
    {
        ESP_LOGW(TAG, "NTP 未同步，使用系统时间");
    }

    return app::common::time::iso8601_timestamp();
}

std::string OtaMgr::build_url(const std::string& server_url, const std::string& path) const
{
    std::string url = server_url;
    if (!url.empty() && url.back() != '/')
    {
        url += '/';
    }
    if (!path.empty() && path.front() == '/')
    {
        url += path.substr(1);
    }
    else
    {
        url += path;
    }
    return url;
}

void OtaMgr::handle_error(OtaStatus new_status, const std::string& error_msg)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        status_ = new_status;
    }
    if (status_callback_)
    {
        status_callback_(new_status);
    }
    if (!error_msg.empty())
    {
        ESP_LOGE(TAG, "%s", error_msg.c_str());
    }
}

std::string OtaMgr::md5_to_string(const unsigned char* md5_bytes) const
{
    char md5_str[33];
    for (int i = 0; i < 16; i++)
    {
        snprintf(md5_str + i * 2, 3, "%02x", md5_bytes[i]);
    }
    md5_str[32] = '\0';
    return std::string(md5_str);
}

Md5ContextRAII::Md5ContextRAII()
    : op_(PSA_HASH_OPERATION_INIT)
    , ready_(false)
{
    static std::once_flag psa_init_once;
    std::call_once(psa_init_once, []() { (void)psa_crypto_init(); });
    psa_status_t s = psa_hash_setup(&op_, PSA_ALG_MD5);
    ready_ = (s == PSA_SUCCESS);
    if (!ready_)
    {
        ESP_LOGE(TAG, "psa_hash_setup(MD5) 失败: %d", static_cast<int>(s));
    }
}

Md5ContextRAII::~Md5ContextRAII()
{
    (void)psa_hash_abort(&op_);
}

bool Md5ContextRAII::update(const uint8_t* data, size_t len)
{
    if (!ready_)
    {
        return false;
    }
    return psa_hash_update(&op_, data, len) == PSA_SUCCESS;
}

bool Md5ContextRAII::finish(unsigned char out[16])
{
    if (!ready_)
    {
        return false;
    }
    size_t hash_len = 0;
    psa_status_t s = psa_hash_finish(&op_, out, 16, &hash_len);
    ready_ = false;
    return (s == PSA_SUCCESS) && (hash_len == 16U);
}

std::string OtaMgr::build_base_json_message(const std::string& type) const
{
    JsonRAII json;
    if (!json.is_valid())
    {
        return "";
    }

    cJSON_AddStringToObject(json.get(), "type", type.c_str());
    cJSON_AddStringToObject(json.get(), "from", device_id_.c_str());
    cJSON_AddStringToObject(json.get(), "to", "ota_server");
    cJSON_AddStringToObject(json.get(), "timestamp", get_timestamp().c_str());

    JsonStringRAII json_str(cJSON_Print(json.get()));
    return json_str.is_valid() ? json_str.to_string() : std::string();
}


std::string OtaMgr::build_check_update_message() const
{
    std::string base_json = build_base_json_message("check_update");
    JsonRAII json(base_json.c_str());
    if (!json.is_valid())
    {
        JsonRAII fallback_json;
        if (!fallback_json.is_valid())
        {
            return "";
        }
        cJSON_AddStringToObject(fallback_json.get(), "type", "check_update");
        cJSON_AddStringToObject(fallback_json.get(), "from", device_id_.c_str());
        cJSON_AddStringToObject(fallback_json.get(), "to", "ota_server");
        cJSON_AddStringToObject(fallback_json.get(), "current_version", current_version_.c_str());
        cJSON_AddStringToObject(fallback_json.get(), "timestamp", get_timestamp().c_str());

        JsonStringRAII json_str(cJSON_Print(fallback_json.get()));
        return json_str.is_valid() ? json_str.to_string() : std::string();
    }

    cJSON_AddStringToObject(json.get(), "current_version", current_version_.c_str());

    JsonStringRAII json_str(cJSON_Print(json.get()));
    return json_str.is_valid() ? json_str.to_string() : std::string();
}

std::string OtaMgr::build_get_firmware_info_message() const
{
    return build_base_json_message("get_firmware_info");
}

std::string OtaMgr::build_request_firmware_message(const FirmwareInfo& info) const
{
    std::string base_json = build_base_json_message("request_firmware");
    JsonRAII json(base_json.c_str());
    if (!json.is_valid())
    {
        JsonRAII fallback_json;
        if (!fallback_json.is_valid())
        {
            return "";
        }
        cJSON_AddStringToObject(fallback_json.get(), "type", "request_firmware");
        cJSON_AddStringToObject(fallback_json.get(), "from", device_id_.c_str());
        cJSON_AddStringToObject(fallback_json.get(), "to", "ota_server");
        cJSON_AddStringToObject(fallback_json.get(), "timestamp", get_timestamp().c_str());

        JsonRAII data;
        if (data.is_valid())
        {
            cJSON_AddStringToObject(data.get(), "name", info.name.c_str());
            cJSON_AddStringToObject(data.get(), "target_version", info.version.c_str());
            cJSON_AddStringToObject(data.get(), "md5", info.md5.c_str());
            cJSON_AddItemToObject(fallback_json.get(), "data", data.release());
        }

        JsonStringRAII json_str(cJSON_Print(fallback_json.get()));
        return json_str.is_valid() ? json_str.to_string() : std::string();
    }

    JsonRAII data;
    if (data.is_valid())
    {
        cJSON_AddStringToObject(data.get(), "name", info.name.c_str());
        cJSON_AddStringToObject(data.get(), "target_version", info.version.c_str());
        cJSON_AddStringToObject(data.get(), "md5", info.md5.c_str());
        cJSON_AddItemToObject(json.get(), "data", data.release());
    }

    JsonStringRAII json_str(cJSON_Print(json.get()));
    return json_str.is_valid() ? json_str.to_string() : std::string();
}

std::string OtaMgr::build_report_status_message(uint8_t status, uint8_t progress) const
{
    std::string base_json = build_base_json_message("report_status");
    JsonRAII json(base_json.c_str());
    if (!json.is_valid())
    {
        JsonRAII fallback_json;
        if (!fallback_json.is_valid())
        {
            return "";
        }
        cJSON_AddStringToObject(fallback_json.get(), "type", "report_status");
        cJSON_AddStringToObject(fallback_json.get(), "from", device_id_.c_str());
        cJSON_AddStringToObject(fallback_json.get(), "to", "ota_server");
        cJSON_AddStringToObject(fallback_json.get(), "timestamp", get_timestamp().c_str());

        JsonRAII data;
        if (data.is_valid())
        {
            cJSON_AddNumberToObject(data.get(), "status", status);
            cJSON_AddNumberToObject(data.get(), "progress", progress);
            cJSON_AddStringToObject(data.get(), "current_version", current_version_.c_str());
            cJSON_AddItemToObject(fallback_json.get(), "data", data.release());
        }

        JsonStringRAII json_str(cJSON_Print(fallback_json.get()));
        return json_str.is_valid() ? json_str.to_string() : std::string();
    }

    JsonRAII data;
    if (data.is_valid())
    {
        cJSON_AddNumberToObject(data.get(), "status", status);
        cJSON_AddNumberToObject(data.get(), "progress", progress);
        cJSON_AddStringToObject(data.get(), "current_version", current_version_.c_str());
        cJSON_AddItemToObject(json.get(), "data", data.release());
    }

    JsonStringRAII json_str(cJSON_Print(json.get()));
    return json_str.is_valid() ? json_str.to_string() : std::string();
}

bool OtaMgr::parse_reply_update(const std::string& json, int& respond, std::string& download_url)
{
    JsonRAII root(json.c_str());
    if (!root.is_valid())
    {
        ESP_LOGE(TAG, "JSON 解析失败: %s", cJSON_GetErrorPtr());
        return false;
    }

    cJSON* respond_item = cJSON_GetObjectItem(root.get(), "respond");
    if (cJSON_IsNumber(respond_item))
    {
        respond = respond_item->valueint;
    }
    else
    {
        return false;
    }

    cJSON* url_item = cJSON_GetObjectItem(root.get(), "download_url");
    if (cJSON_IsString(url_item))
    {
        download_url = url_item->valuestring;
    }

    return true;
}

bool OtaMgr::parse_firmware_info(const std::string& json, FirmwareInfo& info)
{
    JsonRAII root(json.c_str());
    if (!root.is_valid())
    {
        ESP_LOGE(TAG, "JSON 解析失败: %s", cJSON_GetErrorPtr());
        return false;
    }

    cJSON* file_item = cJSON_GetObjectItem(root.get(), "file");
    if (!file_item || !cJSON_IsObject(file_item))
    {
        return false;
    }

    cJSON* version_item = cJSON_GetObjectItem(file_item, "version");
    if (cJSON_IsString(version_item))
    {
        info.version = version_item->valuestring;
    }

    cJSON* name_item = cJSON_GetObjectItem(file_item, "name");
    if (cJSON_IsString(name_item))
    {
        info.name = name_item->valuestring;
    }

    cJSON* size_item = cJSON_GetObjectItem(file_item, "size");
    if (cJSON_IsNumber(size_item))
    {
        info.size = (size_t)size_item->valueint;
    }

    cJSON* info_item = cJSON_GetObjectItem(file_item, "info");
    if (cJSON_IsString(info_item))
    {
        info.info = info_item->valuestring;
    }

    cJSON* md5_item = cJSON_GetObjectItem(file_item, "md5");
    if (cJSON_IsString(md5_item))
    {
        info.md5 = md5_item->valuestring;
    }

    cJSON* time_item = cJSON_GetObjectItem(file_item, "time");
    if (cJSON_IsString(time_item))
    {
        info.time = time_item->valuestring;
    }

    return true;
}

bool OtaMgr::parse_error(const std::string& json, int& code, std::string& message)
{
    JsonRAII root(json.c_str());
    if (!root.is_valid())
    {
        return false;
    }

    cJSON* type_item = cJSON_GetObjectItem(root.get(), "type");
    if (!type_item || !cJSON_IsString(type_item) || strcmp(type_item->valuestring, "error") != 0)
    {
        return false;
    }

    cJSON* data_item = cJSON_GetObjectItem(root.get(), "data");
    if (!data_item || !cJSON_IsObject(data_item))
    {
        return false;
    }

    cJSON* code_item = cJSON_GetObjectItem(data_item, "code");
    if (cJSON_IsNumber(code_item))
    {
        code = code_item->valueint;
    }

    cJSON* message_item = cJSON_GetObjectItem(data_item, "message");
    if (cJSON_IsString(message_item))
    {
        message = message_item->valuestring;
    }

    return true;
}

bool OtaMgr::check_update(const std::string& server_url, int32_t timeout_ms)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_)
        {
            ESP_LOGE(TAG, "OTA 管理器未初始化");
            return false;
        }

        if (status_ == OtaStatus::DOWNLOADING || status_ == OtaStatus::VERIFYING)
        {
            ESP_LOGE(TAG, "OTA 升级进行中，无法检查更新");
            return false;
        }

        status_ = OtaStatus::CHECKING;
    }

    if (status_callback_)
    {
        status_callback_(OtaStatus::CHECKING);
    }

    auto& http_mgr = app::protocol::http::HttpMgr::get_instance();
    std::string url = build_url(server_url, "api/ota/check");
    std::string request_body = build_check_update_message();

    app::protocol::http::HttpResponse response;
    bool http_result = http_mgr.post(url, request_body, response, timeout_ms);

    if (!http_result)
    {
        handle_error(OtaStatus::FAILED, "检查更新失败：HTTP 请求失败");
        return false;
    }

    if (response.status_code != app::protocol::http::HttpStatus::OK)
    {
        char error_buf[64];
        snprintf(error_buf, sizeof(error_buf), "检查更新失败：HTTP 状态码 %d",
                 static_cast<int>(response.status_code_int));
        handle_error(OtaStatus::FAILED, error_buf);
        return false;
    }

    std::string body_str(response.body.begin(), response.body.end());

    int error_code;
    std::string error_message;
    if (parse_error(body_str, error_code, error_message))
    {
        char error_buf[128];
        snprintf(error_buf, sizeof(error_buf), "检查更新失败：%s (错误码: %d)",
                 error_message.c_str(), error_code);
        handle_error(OtaStatus::FAILED, error_buf);
        return false;
    }

    int respond;
    std::string download_url;
    if (!parse_reply_update(body_str, respond, download_url))
    {
        handle_error(OtaStatus::FAILED, "检查更新失败：无法解析服务器响应");
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        status_ = OtaStatus::IDLE;
    }
    if (status_callback_)
    {
        status_callback_(OtaStatus::IDLE);
    }
    return respond == 1 || respond == 2;
}

bool OtaMgr::get_firmware_info(const std::string& server_url, FirmwareInfo& info,
                               int32_t timeout_ms)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!initialized_)
        {
            ESP_LOGE(TAG, "OTA 管理器未初始化");
            return false;
        }
    }

    auto& http_mgr = app::protocol::http::HttpMgr::get_instance();

    std::string url = build_url(server_url, "api/ota/info");
    std::string request_body = build_get_firmware_info_message();

    app::protocol::http::HttpResponse response;
    if (!http_mgr.post(url, request_body, response, timeout_ms))
    {
        ESP_LOGE(TAG, "获取固件信息失败：HTTP 请求失败");
        return false;
    }

    if (response.status_code != app::protocol::http::HttpStatus::OK)
    {
        ESP_LOGE(TAG, "获取固件信息失败：HTTP 状态码 %d",
                 static_cast<int>(response.status_code_int));
        return false;
    }

    std::string body_str(response.body.begin(), response.body.end());

    int error_code;
    std::string error_message;
    if (parse_error(body_str, error_code, error_message))
    {
        ESP_LOGE(TAG, "获取固件信息失败：%s (错误码: %d)", error_message.c_str(), error_code);
        return false;
    }

    if (!parse_firmware_info(body_str, info))
    {
        ESP_LOGE(TAG, "获取固件信息失败：无法解析服务器响应");
        return false;
    }

    return true;
}

bool OtaMgr::start_update(const std::string& server_url, const FirmwareInfo& firmware_info,
                          int32_t timeout_ms)
{
    ESP_LOGI(TAG, "开始启动 OTA 升级，固件版本: %s", firmware_info.version.c_str());

    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!initialized_)
        {
            ESP_LOGE(TAG, "OTA 管理器未初始化");
            return false;
        }

        if (status_ == OtaStatus::DOWNLOADING || status_ == OtaStatus::VERIFYING)
        {
            ESP_LOGE(TAG, "OTA 升级已在进行中");
            return false;
        }
    }

    auto task_config = app::sys::task::Cfg();
    task_config.name = "ota_update";
    task_config.stack_size = 16 * 1024;
    task_config.priority = app::sys::task::Priority::HIGH;
    task_config.core_id = -1;
    task_config.use_psram = true;

    struct UpdateContext
    {
        std::string server_url;
        FirmwareInfo firmware_info;
        int32_t timeout_ms;
        OtaMgr* manager;
        ProgressCallback progress_callback;
        CompleteCallback complete_callback;

        UpdateContext(const std::string& url, const FirmwareInfo& info, int32_t timeout,
                      OtaMgr* mgr, const ProgressCallback& prog_cb, const CompleteCallback& comp_cb)
            : server_url(url)
            , firmware_info(info)
            , timeout_ms(timeout)
            , manager(mgr)
            , progress_callback(prog_cb)
            , complete_callback(comp_cb)
        {
        }
    };

    auto ctx = std::make_shared<UpdateContext>(server_url, firmware_info, timeout_ms, this,
                                               progress_callback_, complete_callback_);

    auto ctx_ptr = std::make_unique<std::shared_ptr<UpdateContext>>(ctx);

    app::sys::task::Task::Func task_function = [](void* param) {
        std::unique_ptr<std::shared_ptr<UpdateContext>> ctx_owner(
            static_cast<std::shared_ptr<UpdateContext>*>(param));
        auto ctx = *ctx_owner;
        auto& manager = *ctx->manager;

        app::sys::task::TaskMgr::delay_ms(10);

        {
            std::lock_guard<std::mutex> lock(manager.mutex_);
            auto& task_mgr = app::sys::task::TaskMgr::get_instance();
            TaskHandle_t task_handle = task_mgr.find("ota_update");
            if (manager.current_update_task_ == nullptr)
            {
                manager.current_update_task_ = task_handle;
            }
            manager.cancelled_ = false;
        }

        manager.update_status(OtaStatus::DOWNLOADING);

        const esp_partition_t* update_partition = nullptr;
        esp_ota_handle_t ota_handle = 0;

        const esp_partition_t* running = esp_ota_get_running_partition();
        esp_ota_img_states_t ota_state;
        if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK)
        {
            if (ota_state == ESP_OTA_IMG_PENDING_VERIFY)
            {
                ESP_LOGW(TAG, "检测到待验证的 OTA 镜像，标记为有效");
                esp_ota_mark_app_valid_cancel_rollback();
            }
        }

        if (running->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0)
        {
            update_partition = esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                                                        ESP_PARTITION_SUBTYPE_APP_OTA_1, nullptr);
        }
        else
        {
            update_partition = esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                                                        ESP_PARTITION_SUBTYPE_APP_OTA_0, nullptr);
        }

        if (update_partition == nullptr)
        {
            ESP_LOGE(TAG, "未找到 OTA 分区");
            manager.update_status(OtaStatus::FAILED);
            if (ctx->complete_callback)
            {
                ctx->complete_callback(false, "未找到 OTA 分区");
            }
            {
                std::lock_guard<std::mutex> lock(manager.mutex_);
                manager.current_update_task_ = nullptr;
            }
            return;
        }

        ESP_LOGI(TAG, "使用 OTA 分区：%s, 偏移: 0x%08x, 大小: %u", update_partition->label,
                 (unsigned int)update_partition->address, (unsigned int)update_partition->size);

        esp_err_t err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "esp_ota_begin 失败: %s", esp_err_to_name(err));
            manager.update_status(OtaStatus::FAILED);
            if (ctx->complete_callback)
            {
                char error_buf[128];
                snprintf(error_buf, sizeof(error_buf), "esp_ota_begin 失败: %s",
                         esp_err_to_name(err));
                ctx->complete_callback(false, error_buf);
            }
            {
                std::lock_guard<std::mutex> lock(manager.mutex_);
                manager.current_update_task_ = nullptr;
            }
            return;
        }

        std::string url = manager.build_url(ctx->server_url, "firmware/" + ctx->firmware_info.name);

        app::protocol::http::HttpRequest request;
        request.url = url;
        request.method = app::protocol::http::HttpMethod::GET;
        request.timeout_ms = ctx->timeout_ms;

        Md5ContextRAII md5_ctx;
        if (!md5_ctx.is_ready())
        {
            ESP_LOGE(TAG, "MD5 上下文初始化失败");
            esp_ota_abort(ota_handle);
            manager.update_status(OtaStatus::FAILED);
            if (ctx->complete_callback)
            {
                ctx->complete_callback(false, "MD5 初始化失败");
            }
            {
                std::lock_guard<std::mutex> lock(manager.mutex_);
                manager.current_update_task_ = nullptr;
            }
            return;
        }

        size_t total_received = 0;
        size_t total_size = ctx->firmware_info.size;
        bool download_ok = false;

        auto& http_mgr = app::protocol::http::HttpMgr::get_instance();

        bool http_success = http_mgr.perform(
            request,
            [&total_size](const app::protocol::http::HttpResponse& resp) {
                if (resp.content_length > 0)
                {
                    total_size = resp.content_length;
                }
                return resp.status_code == app::protocol::http::HttpStatus::OK;
            },
            [ctx, &ota_handle, &md5_ctx, &total_received, &total_size,
             &download_ok](const uint8_t* data, size_t len) -> bool {
                {
                    std::lock_guard<std::mutex> lock(ctx->manager->mutex_);
                    if (ctx->manager->cancelled_)
                    {
                        ESP_LOGW(TAG, "下载已取消");
                        download_ok = false;
                        return false;
                    }
                }

                esp_err_t err = esp_ota_write(ota_handle, data, len);
                if (err != ESP_OK)
                {
                    ESP_LOGE(TAG, "esp_ota_write 失败: %s", esp_err_to_name(err));
                    download_ok = false;
                    return false;
                }

                if (!md5_ctx.update(data, len))
                {
                    ESP_LOGE(TAG, "MD5 更新失败");
                    download_ok = false;
                    return false;
                }
                total_received += len;
                float percent = 0.0f;
                if (total_size > 0)
                {
                    percent = (float)total_received * 100.0f / (float)total_size;
                }

                if (ctx->progress_callback)
                {
                    ctx->progress_callback(total_received, total_size, percent);
                }

                download_ok = true;
                return true;
            });

        bool was_cancelled = false;
        {
            std::lock_guard<std::mutex> lock(manager.mutex_);
            was_cancelled = manager.cancelled_;
        }

        if (!http_success || !download_ok)
        {
            const char* error_msg = was_cancelled ? "下载已取消" : "下载失败";
            ESP_LOGE(TAG, "%s", error_msg);
            esp_ota_abort(ota_handle);
            manager.update_status(OtaStatus::FAILED);
            if (ctx->complete_callback)
            {
                ctx->complete_callback(false, error_msg);
            }
            {
                std::lock_guard<std::mutex> lock(manager.mutex_);
                manager.current_update_task_ = nullptr;
            }
            return;
        }

        unsigned char md5_result[16];
        if (!md5_ctx.finish(md5_result))
        {
            ESP_LOGE(TAG, "MD5 结束计算失败");
            esp_ota_abort(ota_handle);
            manager.update_status(OtaStatus::FAILED);
            if (ctx->complete_callback)
            {
                ctx->complete_callback(false, "MD5 计算失败");
            }
            {
                std::lock_guard<std::mutex> lock(manager.mutex_);
                manager.current_update_task_ = nullptr;
            }
            return;
        }

        std::string md5_str = manager.md5_to_string(md5_result);
        manager.update_status(OtaStatus::VERIFYING);

        std::string expected_md5 = ctx->firmware_info.md5;
        std::string actual_md5 = md5_str;
        std::transform(expected_md5.begin(), expected_md5.end(), expected_md5.begin(), ::tolower);
        std::transform(actual_md5.begin(), actual_md5.end(), actual_md5.begin(), ::tolower);

        if (expected_md5 != actual_md5)
        {
            ESP_LOGE(TAG, "MD5 校验失败：期望=%s, 实际=%s", ctx->firmware_info.md5.c_str(),
                     md5_str.c_str());
            esp_ota_abort(ota_handle);
            manager.update_status(OtaStatus::FAILED);
            if (ctx->complete_callback)
            {
                ctx->complete_callback(false, "MD5 校验失败");
            }
            {
                std::lock_guard<std::mutex> lock(manager.mutex_);
                manager.current_update_task_ = nullptr;
            }
            return;
        }

        err = esp_ota_end(ota_handle);
        if (err != ESP_OK)
        {
            const char* error_msg =
                (err == ESP_ERR_OTA_VALIDATE_FAILED) ? "OTA 镜像验证失败" : "esp_ota_end 失败";
            ESP_LOGE(TAG, "%s: %s", error_msg, esp_err_to_name(err));
            manager.update_status(OtaStatus::FAILED);
            if (ctx->complete_callback)
            {
                char error_buf[128];
                snprintf(error_buf, sizeof(error_buf), "%s: %s", error_msg, esp_err_to_name(err));
                ctx->complete_callback(false, error_buf);
            }
            {
                std::lock_guard<std::mutex> lock(manager.mutex_);
                manager.current_update_task_ = nullptr;
            }
            return;
        }

        err = esp_ota_set_boot_partition(update_partition);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "esp_ota_set_boot_partition 失败: %s", esp_err_to_name(err));
            manager.update_status(OtaStatus::FAILED);
            if (ctx->complete_callback)
            {
                char error_buf[128];
                snprintf(error_buf, sizeof(error_buf), "设置引导分区失败: %s",
                         esp_err_to_name(err));
                ctx->complete_callback(false, error_buf);
            }
            {
                std::lock_guard<std::mutex> lock(manager.mutex_);
                manager.current_update_task_ = nullptr;
            }
            return;
        }

        manager.update_status(OtaStatus::COMPLETED);
        if (ctx->complete_callback)
        {
            ctx->complete_callback(true, "");
        }

        {
            std::lock_guard<std::mutex> lock(manager.mutex_);
            manager.current_update_task_ = nullptr;
        }

        app::sys::task::TaskMgr::delay_ms(1000);
        esp_restart();
    };

    void* task_param = ctx_ptr.release();
    auto task = std::make_unique<app::sys::task::Task>(task_function, task_config, task_param);

    if (!task->start())
    {
        ESP_LOGE(TAG, "启动 OTA 升级任务失败");
        handle_error(OtaStatus::FAILED, "启动 OTA 升级任务失败");
        delete static_cast<std::shared_ptr<UpdateContext>*>(task_param);
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        current_update_task_ = task->handle();
    }

    return true;
}

bool OtaMgr::report_status(const std::string& server_url, uint8_t status, uint8_t progress,
                           int32_t timeout_ms)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!initialized_)
        {
            return false;
        }
    }

    auto& http_mgr = app::protocol::http::HttpMgr::get_instance();

    std::string url = build_url(server_url, "api/ota/status");
    std::string request_body = build_report_status_message(status, progress);

    app::protocol::http::HttpResponse response;
    http_mgr.post(url, request_body, response, timeout_ms);

    return true;
}

bool OtaMgr::cancel()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (status_ != OtaStatus::DOWNLOADING && status_ != OtaStatus::VERIFYING)
        {
            ESP_LOGW(TAG, "当前没有进行中的 OTA 升级");
            return false;
        }

        cancelled_ = true;
        status_ = OtaStatus::IDLE;
    }

    if (status_callback_)
    {
        status_callback_(OtaStatus::IDLE);
    }

    return true;
}

OtaStatus OtaMgr::get_status() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}

bool OtaMgr::is_updating() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return status_ == OtaStatus::DOWNLOADING || status_ == OtaStatus::VERIFYING ||
           status_ == OtaStatus::CHECKING;
}

std::string OtaMgr::get_current_version() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return current_version_;
}


void OtaMgr::set_progress_callback(const ProgressCallback& callback)
{
    std::lock_guard<std::mutex> lock(mutex_);
    progress_callback_ = callback;
}

void OtaMgr::set_status_callback(const StatusCallback& callback)
{
    std::lock_guard<std::mutex> lock(mutex_);
    status_callback_ = callback;
}

void OtaMgr::set_complete_callback(const CompleteCallback& callback)
{
    std::lock_guard<std::mutex> lock(mutex_);
    complete_callback_ = callback;
}


void OtaMgr::update_status(OtaStatus status)
{
    std::lock_guard<std::mutex> lock(mutex_);
    status_ = status;

    if (status_callback_)
    {
        status_callback_(status);
    }
}

} // namespace app::common::ota
