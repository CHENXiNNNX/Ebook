#include "storage.hpp"

#include "storage_usb_msc.hpp"

#include <cstring>
#include <inttypes.h>

#include "config/config.hpp"
#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "sdkconfig.h"
#include "soc/soc_caps.h"
#include "system/task/task.hpp"

#ifndef CONFIG_EBOOK_STORAGE_FORMAT_IF_MOUNT_FAILED
#define CONFIG_EBOOK_STORAGE_FORMAT_IF_MOUNT_FAILED 0
#endif
#ifndef CONFIG_EBOOK_SD_SDMMC_ENABLE
#define CONFIG_EBOOK_SD_SDMMC_ENABLE 0
#endif
#ifndef CONFIG_EBOOK_SD_FORMAT_IF_MOUNT_FAILED
#define CONFIG_EBOOK_SD_FORMAT_IF_MOUNT_FAILED 0
#endif
#ifndef CONFIG_EBOOK_SD_SPEED_HS
#define CONFIG_EBOOK_SD_SPEED_HS 0
#endif
#ifndef CONFIG_EBOOK_SD_MAX_FILES
#define CONFIG_EBOOK_SD_MAX_FILES 5
#endif
#ifndef CONFIG_EBOOK_INTERNAL_MAX_FILES
#define CONFIG_EBOOK_INTERNAL_MAX_FILES 10
#endif
#ifndef CONFIG_EBOOK_SD_HOTPLUG_SERVICE_MS
#define CONFIG_EBOOK_SD_HOTPLUG_SERVICE_MS 500
#endif

namespace app::common::storage {

namespace {

const char* const TAG = "Storage";

bool pin_valid(gpio_num_t pin)
{
    return pin != GPIO_NUM_NC;
}

bool is_four_line_mode()
{
    return pin_valid(config::TF_CARD_D1) && pin_valid(config::TF_CARD_D2) &&
           pin_valid(config::TF_CARD_D3);
}

bool has_hardware_cd()
{
    return pin_valid(config::TF_CARD_CD);
}

bool sd_pins_configured()
{
    return pin_valid(config::TF_CARD_CLK) && pin_valid(config::TF_CARD_CMD) &&
           pin_valid(config::TF_CARD_D0);
}

void log_sd_pin_config()
{
    ESP_LOGI(TAG, "SDMMC: CLK=%d CMD=%d D0=%d D1=%d D2=%d D3=%d CD=%d → %s%s",
             static_cast<int>(config::TF_CARD_CLK), static_cast<int>(config::TF_CARD_CMD),
             static_cast<int>(config::TF_CARD_D0), static_cast<int>(config::TF_CARD_D1),
             static_cast<int>(config::TF_CARD_D2), static_cast<int>(config::TF_CARD_D3),
             static_cast<int>(config::TF_CARD_CD), is_four_line_mode() ? "4线" : "1线",
             has_hardware_cd() ? " 硬件CD" : " 软件轮询");
}

#if SOC_SDMMC_HOST_SUPPORTED
bool sd_cd_card_present()
{
    constexpr gpio_num_t cd = config::TF_CARD_CD;
    if constexpr (cd == GPIO_NUM_NC)
    {
        return false;
    }
    else
    {
        gpio_config_t io = {};
        io.pin_bit_mask = 1ULL << static_cast<unsigned>(cd);
        io.mode = GPIO_MODE_INPUT;
        io.pull_up_en = GPIO_PULLUP_ENABLE;
        io.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io.intr_type = GPIO_INTR_DISABLE;
        gpio_config(&io);
        return gpio_get_level(cd) == 0;
    }
}
#endif

} // namespace

StorageMgr& StorageMgr::get_instance()
{
    static StorageMgr instance;
    return instance;
}

esp_err_t StorageMgr::init_defaults()
{
    std::lock_guard<std::mutex> lock(mutex_);

    esp_err_t err = mount_internal_locked();
    if (err != ESP_OK)
    {
        return err;
    }

    err = mount_assets_locked();
    if (err != ESP_OK)
    {
        (void)umount_internal_locked();
        return err;
    }

    return ESP_OK;
}

void StorageMgr::deinit()
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (sd_mounted_)
    {
        (void)umount_sd_locked();
    }
    if (usb_mounted_)
    {
        (void)umount_usb_locked();
    }
    if (assets_mounted_)
    {
        (void)umount_assets_locked();
    }
    if (internal_mounted_)
    {
        (void)umount_internal_locked();
    }
}

esp_err_t StorageMgr::mount_assets()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return mount_assets_locked();
}

esp_err_t StorageMgr::umount_assets()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return umount_assets_locked();
}

esp_err_t StorageMgr::mount_internal()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return mount_internal_locked();
}

esp_err_t StorageMgr::umount_internal()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return umount_internal_locked();
}

esp_err_t StorageMgr::mount_sd()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return mount_sd_locked(true);
}

esp_err_t StorageMgr::umount_sd()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return umount_sd_locked();
}

esp_err_t StorageMgr::mount_usb()
{
    ESP_LOGW(TAG, "mount_usb: 外接 U 盘 (Host) 未实现");
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t StorageMgr::expose_internal_to_pc()
{
#if CONFIG_TINYUSB_MSC_ENABLED
    std::lock_guard<std::mutex> lock(mutex_);
    if (!internal_mounted_)
    {
        const esp_err_t err = mount_internal_locked();
        if (err != ESP_OK)
        {
            return err;
        }
    }
    return usb_msc_expose_to_host();
#else
    ESP_LOGW(TAG, "expose_internal_to_pc: TinyUSB MSC 未启用");
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t StorageMgr::reclaim_internal_from_pc()
{
#if CONFIG_TINYUSB_MSC_ENABLED
    std::lock_guard<std::mutex> lock(mutex_);
    if (!usb_msc_is_initialized())
    {
        return ESP_ERR_INVALID_STATE;
    }
    return usb_msc_reclaim_for_app();
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

bool StorageMgr::is_internal_exposed_to_pc() const
{
#if CONFIG_TINYUSB_MSC_ENABLED
    std::lock_guard<std::mutex> lock(mutex_);
    return usb_msc_is_exposed_to_host();
#else
    return false;
#endif
}

esp_err_t StorageMgr::umount_usb()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return umount_usb_locked();
}

void StorageMgr::service_internal_recovery()
{
#if CONFIG_TINYUSB_MSC_ENABLED
    std::lock_guard<std::mutex> lock(mutex_);
    if (!internal_mounted_)
    {
        return;
    }
    usb_msc_service_recovery();
#endif
}

void StorageMgr::service_sd_hotplug()
{
#if !SOC_SDMMC_HOST_SUPPORTED || !CONFIG_EBOOK_SD_SDMMC_ENABLE
    return;
#else
    const uint32_t now_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
    static uint32_t s_last_ms = 0;
    constexpr uint32_t interval = CONFIG_EBOOK_SD_HOTPLUG_SERVICE_MS;
    if ((now_ms - s_last_ms) < interval)
    {
        return;
    }
    s_last_ms = now_ms;

    std::lock_guard<std::mutex> lock(mutex_);

    if (has_hardware_cd())
    {
        const bool inserted = sd_cd_card_present();
        if (!inserted && sd_mounted_)
        {
            (void)umount_sd_locked();
            return;
        }
        if (inserted && !sd_mounted_)
        {
            (void)mount_sd_locked(false);
        }
    }
    else
    {
        if (!sd_mounted_)
        {
            (void)mount_sd_locked(false);
        }
        else
        {
            auto* card = static_cast<sdmmc_card_t*>(sd_card_);
            if (card == nullptr)
            {
                sd_mounted_ = false;
                return;
            }
            if (sdmmc_get_status(card) != ESP_OK)
            {
                ESP_LOGW(TAG, "TF 软件检测: 卡无响应, 卸载 /sd");
                (void)umount_sd_locked();
            }
        }
    }
#endif
}

bool StorageMgr::is_mounted(MountKind kind) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    switch (kind)
    {
        case MountKind::Internal:
#if CONFIG_TINYUSB_MSC_ENABLED
            return internal_mounted_ && usb_msc_is_available_to_app();
#else
            return internal_mounted_;
#endif
        case MountKind::Sd:
            return sd_mounted_;
        case MountKind::Usb:
            return usb_mounted_;
        case MountKind::Assets:
            return assets_mounted_;
        default:
            return false;
    }
}

esp_err_t StorageMgr::query_assets_usage(size_t* total_out, size_t* used_out) const
{
    if (total_out == nullptr || used_out == nullptr)
    {
        return ESP_ERR_INVALID_ARG;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (!assets_mounted_)
    {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_littlefs_info(k_label_assets, total_out, used_out);
}

esp_err_t StorageMgr::query_internal_usage(size_t* total_out, size_t* used_out) const
{
    if (total_out == nullptr || used_out == nullptr)
    {
        return ESP_ERR_INVALID_ARG;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (!internal_mounted_)
    {
        return ESP_ERR_INVALID_STATE;
    }
#if CONFIG_TINYUSB_MSC_ENABLED
    if (!usb_msc_is_available_to_app())
    {
        return ESP_ERR_INVALID_STATE;
    }
#endif
    uint64_t total_b = 0;
    uint64_t free_b  = 0;
    esp_err_t err    = esp_vfs_fat_info(k_path_internal, &total_b, &free_b);
    if (err != ESP_OK)
    {
        return err;
    }
    *total_out = static_cast<size_t>(total_b);
    *used_out  = static_cast<size_t>((total_b >= free_b) ? (total_b - free_b) : 0);
    return ESP_OK;
}

esp_err_t StorageMgr::query_sd_usage(uint64_t* total_bytes_out, uint64_t* used_bytes_out) const
{
    if (total_bytes_out == nullptr || used_bytes_out == nullptr)
    {
        return ESP_ERR_INVALID_ARG;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (!sd_mounted_)
    {
        return ESP_ERR_INVALID_STATE;
    }

    uint64_t free_b = 0;
    esp_err_t err = esp_vfs_fat_info(k_path_sd, total_bytes_out, &free_b);
    if (err != ESP_OK)
    {
        return err;
    }

    *used_bytes_out = (*total_bytes_out >= free_b) ? (*total_bytes_out - free_b) : 0;
    return ESP_OK;
}

esp_err_t StorageMgr::mount_internal_locked()
{
    if (internal_mounted_)
    {
        return ESP_OK;
    }

#if CONFIG_TINYUSB_MSC_ENABLED
    const esp_err_t err = usb_msc_init_internal();
    if (err != ESP_OK)
    {
        return err;
    }

    uint64_t total_b = 0;
    uint64_t free_b  = 0;
    if (esp_vfs_fat_info(k_path_internal, &total_b, &free_b) == ESP_OK)
    {
        const uint64_t used_b = (total_b >= free_b) ? (total_b - free_b) : 0;
        ESP_LOGI(TAG, "userdata FAT (MSC): %" PRIu64 " 总计, %" PRIu64 " 已用, %" PRIu64 " 空闲",
                 total_b, used_b, free_b);
    }

    internal_mounted_ = true;
    wl_handle_        = WL_INVALID_HANDLE;
    return ESP_OK;
#else
    esp_vfs_fat_mount_config_t mount_cfg = VFS_FAT_MOUNT_DEFAULT_CONFIG();
    mount_cfg.format_if_mount_failed     = CONFIG_EBOOK_STORAGE_FORMAT_IF_MOUNT_FAILED != 0;
    mount_cfg.max_files                  = CONFIG_EBOOK_INTERNAL_MAX_FILES;
    mount_cfg.allocation_unit_size       = CONFIG_WL_SECTOR_SIZE;

    wl_handle_t wl = WL_INVALID_HANDLE;
    esp_err_t err =
        esp_vfs_fat_spiflash_mount_rw_wl(k_path_internal, k_label_userdata, &mount_cfg, &wl);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "userdata FAT 挂载失败: %s", esp_err_to_name(err));
        return err;
    }

    wl_handle_ = wl;

    uint64_t total_b = 0;
    uint64_t free_b  = 0;
    if (esp_vfs_fat_info(k_path_internal, &total_b, &free_b) == ESP_OK)
    {
        const uint64_t used_b = (total_b >= free_b) ? (total_b - free_b) : 0;
        ESP_LOGI(TAG, "userdata FAT: %" PRIu64 " 总计, %" PRIu64 " 已用, %" PRIu64 " 空闲",
                 total_b, used_b, free_b);
    }

    internal_mounted_ = true;
    return ESP_OK;
#endif
}

esp_err_t StorageMgr::umount_internal_locked()
{
    if (!internal_mounted_)
    {
        return ESP_OK;
    }

#if CONFIG_TINYUSB_MSC_ENABLED
    if (usb_msc_is_exposed_to_host())
    {
        (void)usb_msc_reclaim_for_app();
    }
    usb_msc_shutdown();
    wl_handle_        = WL_INVALID_HANDLE;
    internal_mounted_ = false;
    return ESP_OK;
#else
    esp_err_t err = esp_vfs_fat_spiflash_unmount_rw_wl(k_path_internal, wl_handle_);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "userdata FAT 卸载失败: %s", esp_err_to_name(err));
        return err;
    }

    wl_handle_        = WL_INVALID_HANDLE;
    internal_mounted_ = false;
    return ESP_OK;
#endif
}

esp_err_t StorageMgr::mount_assets_locked()
{
    if (assets_mounted_)
    {
        return ESP_OK;
    }

    esp_vfs_littlefs_conf_t conf{};
    conf.base_path              = k_path_assets;
    conf.partition_label        = k_label_assets;
    conf.format_if_mount_failed = CONFIG_EBOOK_STORAGE_FORMAT_IF_MOUNT_FAILED ? 1 : 0;
    conf.read_only              = 1;
    conf.dont_mount             = 0;
    conf.grow_on_mount          = 0;

    esp_err_t err = esp_vfs_littlefs_register(&conf);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "assets LittleFS 挂载失败: %s", esp_err_to_name(err));
        return err;
    }

    size_t total = 0;
    size_t used  = 0;
    if (esp_littlefs_info(k_label_assets, &total, &used) == ESP_OK)
    {
        ESP_LOGI(TAG, "assets LittleFS (ro): %u 总计, %u 已用",
                 static_cast<unsigned>(total), static_cast<unsigned>(used));
    }

    assets_mounted_ = true;
    return ESP_OK;
}

esp_err_t StorageMgr::umount_assets_locked()
{
    if (!assets_mounted_)
    {
        return ESP_OK;
    }

    esp_err_t err = esp_vfs_littlefs_unregister(k_label_assets);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "注销 assets LittleFS 失败: %s", esp_err_to_name(err));
        return err;
    }

    assets_mounted_ = false;
    return ESP_OK;
}

esp_err_t StorageMgr::mount_sd_locked(bool log_errors)
{
#if !SOC_SDMMC_HOST_SUPPORTED
    ESP_LOGW(TAG, "SoC 不支持 SDMMC");
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (!CONFIG_EBOOK_SD_SDMMC_ENABLE)
    {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (sd_mounted_)
    {
        return ESP_OK;
    }

    if (!sd_pins_configured())
    {
        ESP_LOGE(TAG, "SDMMC 基本引脚 (CLK/CMD/D0) 未配置");
        return ESP_ERR_INVALID_STATE;
    }

    const bool four_line = is_four_line_mode();
    log_sd_pin_config();

    app::sys::task::TaskMgr::delay_ms(200);

    esp_vfs_fat_mount_config_t mount_cfg = VFS_FAT_MOUNT_DEFAULT_CONFIG();
    mount_cfg.format_if_mount_failed = CONFIG_EBOOK_SD_FORMAT_IF_MOUNT_FAILED != 0;
    mount_cfg.max_files = CONFIG_EBOOK_SD_MAX_FILES;
    mount_cfg.allocation_unit_size = 16 * 1024;

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.unaligned_multi_block_rw_max_chunk_size = 8;
#if CONFIG_EBOOK_SD_SPEED_HS
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
#else
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;
#endif

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = four_line ? 4 : 1;

#if SOC_SDMMC_USE_GPIO_MATRIX
    slot.clk = config::TF_CARD_CLK;
    slot.cmd = config::TF_CARD_CMD;
    slot.d0 = config::TF_CARD_D0;
    if (four_line)
    {
        slot.d1 = config::TF_CARD_D1;
        slot.d2 = config::TF_CARD_D2;
        slot.d3 = config::TF_CARD_D3;
    }
    if (has_hardware_cd())
    {
        slot.cd = config::TF_CARD_CD;
    }
#endif

    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    sdmmc_card_t* card = nullptr;
    esp_err_t err = esp_vfs_fat_sdmmc_mount(k_path_sd, &host, &slot, &mount_cfg, &card);

    if (err == ESP_ERR_TIMEOUT)
    {
        ESP_LOGW(TAG, "SD 首次挂载超时, 400ms 后重试");
        app::sys::task::TaskMgr::delay_ms(400);
        err = esp_vfs_fat_sdmmc_mount(k_path_sd, &host, &slot, &mount_cfg, &card);
    }

    if (err != ESP_OK)
    {
        if (log_errors)
        {
            ESP_LOGE(TAG, "SD 挂载失败 (/sd): %s (0x%x)", esp_err_to_name(err), err);
        }
        return err;
    }

    sd_card_ = card;
    sd_mounted_ = true;
    ESP_LOGI(TAG, "SD 已挂载至 /sd, 卡名: %s", card->cid.name);

    uint64_t fat_total = 0;
    uint64_t fat_free = 0;
    if (esp_vfs_fat_info(k_path_sd, &fat_total, &fat_free) == ESP_OK)
    {
        const uint64_t used = (fat_total >= fat_free) ? (fat_total - fat_free) : 0;
        ESP_LOGI(TAG, "SD FAT: %" PRIu64 " 总计, %" PRIu64 " 已用, %" PRIu64 " 空闲", fat_total,
                 used, fat_free);
    }

    return ESP_OK;
#endif
}

esp_err_t StorageMgr::umount_sd_locked()
{
#if !SOC_SDMMC_HOST_SUPPORTED
    sd_mounted_ = false;
    sd_card_ = nullptr;
    return ESP_OK;
#else
    if (!sd_mounted_ || sd_card_ == nullptr)
    {
        sd_mounted_ = false;
        sd_card_ = nullptr;
        return ESP_OK;
    }

    auto* card = static_cast<sdmmc_card_t*>(sd_card_);
    esp_err_t err = esp_vfs_fat_sdcard_unmount(k_path_sd, card);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "卸载 SD (/sd) 失败: %s", esp_err_to_name(err));
        return err;
    }

    sd_card_ = nullptr;
    sd_mounted_ = false;
    return ESP_OK;
#endif
}

esp_err_t StorageMgr::umount_usb_locked()
{
    usb_mounted_ = false;
    return ESP_OK;
}

MountKind mount_kind_from_path_prefix(const char* path)
{
    if (path == nullptr || path[0] != '/')
    {
        return MountKind::Invalid;
    }
    if (path_has_prefix(path, k_path_assets))
    {
        return MountKind::Assets;
    }
    if (path_has_prefix(path, k_path_internal))
    {
        return MountKind::Internal;
    }
    if (path_has_prefix(path, k_path_sd))
    {
        return MountKind::Sd;
    }
    if (path_has_prefix(path, k_path_usb))
    {
        return MountKind::Usb;
    }
    return MountKind::Invalid;
}

} // namespace app::common::storage
