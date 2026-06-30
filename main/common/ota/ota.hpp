#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include "cJSON.h"
#include "psa/crypto.h"

namespace app::common::ota {

/** @brief cJSON 对象 RAII */
class JsonRAII
{
  public:
    explicit JsonRAII(const char* json_str)
        : json_(cJSON_Parse(json_str))
    {
    }

    JsonRAII()
        : json_(cJSON_CreateObject())
    {
    }

    JsonRAII(const JsonRAII&) = delete;
    JsonRAII& operator=(const JsonRAII&) = delete;

    JsonRAII(JsonRAII&& other) noexcept
        : json_(other.json_)
    {
        other.json_ = nullptr;
    }

    JsonRAII& operator=(JsonRAII&& other) noexcept
    {
        if (this != &other)
        {
            reset();
            json_ = other.json_;
            other.json_ = nullptr;
        }
        return *this;
    }

    ~JsonRAII()
    {
        reset();
    }

    cJSON* get() const
    {
        return json_;
    }

    bool is_valid() const
    {
        return json_ != nullptr;
    }

    operator cJSON*() const
    {
        return json_;
    }

    void reset()
    {
        if (json_ != nullptr)
        {
            cJSON_Delete(json_);
            json_ = nullptr;
        }
    }

    cJSON* release()
    {
        cJSON* result = json_;
        json_ = nullptr;
        return result;
    }

  private:
    cJSON* json_;
};

/** @brief cJSON_Print 返回字符串 RAII */
class JsonStringRAII
{
  public:
    explicit JsonStringRAII(char* json_str)
        : str_(json_str)
    {
    }

    JsonStringRAII(const JsonStringRAII&) = delete;
    JsonStringRAII& operator=(const JsonStringRAII&) = delete;

    JsonStringRAII(JsonStringRAII&& other) noexcept
        : str_(other.str_)
    {
        other.str_ = nullptr;
    }

    JsonStringRAII& operator=(JsonStringRAII&& other) noexcept
    {
        if (this != &other)
        {
            reset();
            str_ = other.str_;
            other.str_ = nullptr;
        }
        return *this;
    }

    ~JsonStringRAII()
    {
        reset();
    }

    char* get() const
    {
        return str_;
    }

    bool is_valid() const
    {
        return str_ != nullptr;
    }

    std::string to_string() const
    {
        return str_ ? std::string(str_) : std::string();
    }

    void reset()
    {
        if (str_ != nullptr)
        {
            free(str_);
            str_ = nullptr;
        }
    }

    char* release()
    {
        char* result = str_;
        str_ = nullptr;
        return result;
    }

  private:
    char* str_;
};

/** @brief PSA Crypto MD5（ESP-IDF 6 / Mbed TLS 4，无 mbedtls/md5.h） */
class Md5ContextRAII
{
  public:
    Md5ContextRAII();
    Md5ContextRAII(const Md5ContextRAII&) = delete;
    Md5ContextRAII& operator=(const Md5ContextRAII&) = delete;

    ~Md5ContextRAII();

    bool is_ready() const
    {
        return ready_;
    }

    bool update(const uint8_t* data, size_t len);
    bool finish(unsigned char out[16]);

  private:
    psa_hash_operation_t op_;
    bool ready_;
};

enum class OtaStatus : uint8_t
{
    IDLE = 0,
    CHECKING = 1,
    DOWNLOADING = 2,
    VERIFYING = 3,
    COMPLETED = 4,
    FAILED = 5
};

struct FirmwareInfo
{
    std::string version;
    std::string name;
    size_t size;
    std::string info;
    std::string md5;
    std::string time;
};

using ProgressCallback = std::function<void(size_t received, size_t total, float percent)>;
using StatusCallback = std::function<void(OtaStatus status)>;
using CompleteCallback = std::function<void(bool success, const std::string& error)>;

/** @brief OTA 单例：检查更新、下载、校验、上报 */
class OtaMgr
{
  public:
    static OtaMgr& get_instance();

    bool init(const std::string& device_id, const std::string& current_version);
    void deinit();

    bool check_update(const std::string& server_url, int32_t timeout_ms = 10000);
    bool get_firmware_info(const std::string& server_url, FirmwareInfo& info,
                           int32_t timeout_ms = 10000);
    bool start_update(const std::string& server_url, const FirmwareInfo& firmware_info,
                      int32_t timeout_ms = 120000);
    bool report_status(const std::string& server_url, uint8_t status, uint8_t progress,
                       int32_t timeout_ms = 5000);
    bool cancel();

    OtaStatus get_status() const;
    void set_progress_callback(const ProgressCallback& callback);
    void set_status_callback(const StatusCallback& callback);
    void set_complete_callback(const CompleteCallback& callback);
    bool is_updating() const;
    std::string get_current_version() const;

  private:
    OtaMgr() = default;
    ~OtaMgr()
    {
        deinit();
    }
    OtaMgr(const OtaMgr&) = delete;
    OtaMgr& operator=(const OtaMgr&) = delete;

    std::string get_timestamp() const;
    std::string build_check_update_message() const;
    std::string build_get_firmware_info_message() const;
    std::string build_request_firmware_message(const FirmwareInfo& info) const;
    std::string build_report_status_message(uint8_t status, uint8_t progress) const;
    bool parse_reply_update(const std::string& json, int& respond, std::string& download_url);
    bool parse_firmware_info(const std::string& json, FirmwareInfo& info);
    bool parse_error(const std::string& json, int& code, std::string& message);
    void update_status(OtaStatus status);
    std::string build_url(const std::string& server_url, const std::string& path) const;
    void handle_error(OtaStatus new_status, const std::string& error_msg = "");
    std::string md5_to_string(const unsigned char* md5_bytes) const;
    std::string build_base_json_message(const std::string& type) const;

    mutable std::mutex mutex_;
    bool initialized_;
    std::string device_id_;
    std::string current_version_;
    OtaStatus status_;
    ProgressCallback progress_callback_;
    StatusCallback status_callback_;
    CompleteCallback complete_callback_;
    void* current_update_task_;
    volatile bool cancelled_;
};

} // namespace app::common::ota
