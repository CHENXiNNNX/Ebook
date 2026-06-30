#pragma once

#include <cstdint>
#include <cstring>
#include <mutex>

#include "esp_err.h"
#include "wear_levelling.h"

namespace app::common::storage {

enum class MountKind : uint8_t
{
    Internal = 0,
    Sd = 1,
    Usb = 2,
    Assets = 3,
    Invalid = 0xFF,
};

/**
 * @brief 挂载点单例；TF 线宽与 CD 由 config.hpp 的 TF_CARD_* 推断
 */
class StorageMgr
{
  public:
    static StorageMgr& get_instance();

    StorageMgr(const StorageMgr&) = delete;
    StorageMgr& operator=(const StorageMgr&) = delete;

    /** @brief 挂载 userdata (/int) 与 assets (/assets) */
    esp_err_t init_defaults();

    void deinit();

    esp_err_t mount_assets();
    esp_err_t umount_assets();

    esp_err_t mount_internal();
    esp_err_t umount_internal();

    esp_err_t mount_sd();
    esp_err_t umount_sd();

    void service_sd_hotplug();
    void service_internal_recovery();

    esp_err_t mount_usb();
    esp_err_t umount_usb();

    esp_err_t expose_internal_to_pc();
    esp_err_t reclaim_internal_from_pc();
    bool is_internal_exposed_to_pc() const;

    bool is_mounted(MountKind kind) const;

    esp_err_t query_assets_usage(size_t* total_out, size_t* used_out) const;
    esp_err_t query_internal_usage(size_t* total_out, size_t* used_out) const;
    esp_err_t query_sd_usage(uint64_t* total_bytes_out, uint64_t* used_bytes_out) const;

  private:
    StorageMgr() = default;
    ~StorageMgr()
    {
        deinit();
    }

    esp_err_t mount_assets_locked();
    esp_err_t umount_assets_locked();
    esp_err_t mount_internal_locked();
    esp_err_t umount_internal_locked();
    esp_err_t mount_sd_locked(bool log_errors = true);
    esp_err_t umount_sd_locked();
    esp_err_t umount_usb_locked();

    mutable std::mutex mutex_;
    bool assets_mounted_{false};
    bool internal_mounted_{false};
    bool sd_mounted_{false};
    bool usb_mounted_{false};
    void* sd_card_{nullptr};
    wl_handle_t wl_handle_{WL_INVALID_HANDLE};
};

inline constexpr const char* k_path_internal = "/int";
inline constexpr const char* k_path_sd = "/sd";
inline constexpr const char* k_path_usb = "/usb";
inline constexpr const char* k_path_assets = "/assets";

inline constexpr const char* k_label_assets = "assets";
inline constexpr const char* k_label_userdata = "userdata";

inline bool path_has_prefix(const char* path, const char* prefix)
{
    if (path == nullptr || prefix == nullptr)
        return false;
    const size_t n = std::strlen(prefix);
    if (std::strncmp(path, prefix, n) != 0)
        return false;
    return path[n] == '\0' || path[n] == '/';
}

inline bool path_is_system(const char* path)
{
    return path_has_prefix(path, k_path_assets);
}

MountKind mount_kind_from_path_prefix(const char* path);

} // namespace app::common::storage
