#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace app::protocol::ntp {

enum class SyncStatus
{
    RESET,
    IN_PROGRESS,
    COMPLETED,
    FAILED,
};

enum class SyncMode
{
    IMMEDIATE,
    SMOOTH,
};

using SyncCallback = std::function<void(SyncStatus status)>;

/** @brief SNTP 时间同步（最多 3 个服务器） */
class NtpMgr
{
  public:
    static NtpMgr& get_instance();

    bool init();
    void deinit();

    bool configure(const std::vector<std::string>& servers,
                   SyncMode sync_mode = SyncMode::IMMEDIATE);

    bool start();
    void stop();

    bool wait_sync(uint32_t timeout_ms);

    void set_sync_callback(SyncCallback callback);

    /** 时区字符串，如 "CST-8" */
    bool set_timezone(const char* tz);

    SyncStatus get_sync_status() const;
    bool is_init() const;
    bool is_started() const;

  private:
    NtpMgr() = default;
    ~NtpMgr();
    NtpMgr(const NtpMgr&) = delete;
    NtpMgr& operator=(const NtpMgr&) = delete;

    static void sntp_sync_callback(struct timeval* tv);

    mutable std::mutex mutex_;
    bool initialized_{false};
    bool started_{false};
    SyncStatus sync_status_{SyncStatus::RESET};
    SyncCallback sync_callback_;
    std::vector<std::string> servers_{};
    SyncMode sync_mode_{SyncMode::IMMEDIATE};
};

} // namespace app::protocol::ntp
