#include "storage_usb_msc.hpp"

#if CONFIG_TINYUSB_MSC_ENABLED

#include "storage.hpp"

#include "esp_log.h"
#include "esp_partition.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_msc.h"
#include "tusb.h"
#include "wear_levelling.h"

#ifndef CONFIG_EBOOK_STORAGE_FORMAT_IF_MOUNT_FAILED
#define CONFIG_EBOOK_STORAGE_FORMAT_IF_MOUNT_FAILED 0
#endif
#ifndef CONFIG_EBOOK_INTERNAL_MAX_FILES
#define CONFIG_EBOOK_INTERNAL_MAX_FILES 10
#endif

namespace app::common::storage {

namespace {

const char* const TAG = "UsbMsc";

constexpr uint32_t kMountWaitMs = 8000;

wl_handle_t                  s_wl{WL_INVALID_HANDLE};
tinyusb_msc_storage_handle_t s_storage{nullptr};
bool                         s_tinyusb_installed{false};

static char s_base_path[] = "/int";

SemaphoreHandle_t s_mount_sem{nullptr};
volatile bool     s_mount_waiting{false};
esp_err_t         s_mount_wait_err{ESP_OK};

volatile bool s_host_was_active{false};
uint8_t       s_idle_streak{0};
uint8_t       s_recovery_fail_streak{0};

constexpr uint8_t kRecoveryMaxStreak = 12;
// S3 无 VBUS 检测: 直接拔线不会触发安全弹出, 仅表现为总线挂起
constexpr uint8_t kIdleStreakReclaim = 3;

bool internal_vfs_ok()
{
    uint64_t total_b = 0;
    uint64_t free_b  = 0;
    return esp_vfs_fat_info(s_base_path, &total_b, &free_b) == ESP_OK;
}

void finish_mount_wait(esp_err_t err)
{
    if (!s_mount_waiting)
    {
        return;
    }
    s_mount_wait_err = err;
    s_mount_waiting  = false;
    if (s_mount_sem != nullptr)
    {
        (void)xSemaphoreGive(s_mount_sem);
    }
}

void on_usb_device_event(tinyusb_event_t* event, void* arg)
{
    (void)arg;
    if (event == nullptr)
    {
        return;
    }

    if (event->id == TINYUSB_EVENT_ATTACHED)
    {
        s_host_was_active = true;
    }
    else if (event->id == TINYUSB_EVENT_DETACHED)
    {
        ESP_LOGI(TAG, "USB 主机已断开");
        s_host_was_active = false;
    }
}

void on_storage_event(tinyusb_msc_storage_handle_t handle, tinyusb_msc_event_t* event, void* arg)
{
    (void)handle;
    (void)arg;
    if (event == nullptr)
    {
        return;
    }

    switch (event->id)
    {
        case TINYUSB_MSC_EVENT_MOUNT_COMPLETE:
            ESP_LOGI(TAG, "挂载完成: %s",
                     event->mount_point == TINYUSB_MSC_STORAGE_MOUNT_USB ? "USB 主机" : "应用");
            finish_mount_wait(ESP_OK);
            break;
        case TINYUSB_MSC_EVENT_MOUNT_FAILED:
        case TINYUSB_MSC_EVENT_FORMAT_REQUIRED:
        case TINYUSB_MSC_EVENT_FORMAT_FAILED:
            ESP_LOGW(TAG, "挂载事件 %d", static_cast<int>(event->id));
            finish_mount_wait(ESP_FAIL);
            break;
        default:
            break;
    }
}

/**
 * 必须在 tinyusb_msc_set_storage_mount_point() 之前调用：
 * 无主机时切换是同步完成的，事件会在 set 返回前就触发，
 * 若先 set 再布防等待，事件会被丢掉导致空等超时。
 */
void arm_mount_wait()
{
    while (xSemaphoreTake(s_mount_sem, 0) == pdTRUE)
    {
    }
    s_mount_wait_err = ESP_ERR_TIMEOUT;
    s_mount_waiting  = true;
}

esp_err_t wait_mount_switch(tinyusb_msc_mount_point_t target)
{
    if (xSemaphoreTake(s_mount_sem, pdMS_TO_TICKS(kMountWaitMs)) != pdTRUE)
    {
        s_mount_waiting = false;

        // 兜底：事件可能丢失, 以实际挂载状态为准
        tinyusb_msc_mount_point_t cur = TINYUSB_MSC_STORAGE_MOUNT_USB;
        if (tinyusb_msc_get_storage_mount_point(s_storage, &cur) == ESP_OK && cur == target)
        {
            if (target != TINYUSB_MSC_STORAGE_MOUNT_APP || internal_vfs_ok())
            {
                return ESP_OK;
            }
        }
        ESP_LOGE(TAG, "挂载切换超时");
        return ESP_ERR_TIMEOUT;
    }

    return s_mount_wait_err;
}

bool needs_mount_reset_for_app()
{
    if (s_storage == nullptr || internal_vfs_ok())
    {
        return false;
    }

    tinyusb_msc_mount_point_t current = TINYUSB_MSC_STORAGE_MOUNT_USB;
    if (tinyusb_msc_get_storage_mount_point(s_storage, &current) != ESP_OK)
    {
        return true;
    }

    return current == TINYUSB_MSC_STORAGE_MOUNT_APP;
}

esp_err_t switch_mount_point(tinyusb_msc_mount_point_t target)
{
    if (s_storage == nullptr)
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (target == TINYUSB_MSC_STORAGE_MOUNT_APP && internal_vfs_ok())
    {
        return ESP_OK;
    }

    if (target == TINYUSB_MSC_STORAGE_MOUNT_APP && needs_mount_reset_for_app())
    {
        ESP_LOGW(TAG, "挂载状态异常, 经 USB 复位后挂回 /int");
        (void)switch_mount_point(TINYUSB_MSC_STORAGE_MOUNT_USB);
    }

    tinyusb_msc_mount_point_t current = TINYUSB_MSC_STORAGE_MOUNT_USB;
    if (tinyusb_msc_get_storage_mount_point(s_storage, &current) == ESP_OK &&
        current == target && target == TINYUSB_MSC_STORAGE_MOUNT_USB)
    {
        return ESP_OK;
    }

    if (current == target && target == TINYUSB_MSC_STORAGE_MOUNT_APP && internal_vfs_ok())
    {
        return ESP_OK;
    }

    if (s_mount_sem == nullptr)
    {
        return ESP_ERR_INVALID_STATE;
    }

    arm_mount_wait();

    const esp_err_t set_err = tinyusb_msc_set_storage_mount_point(s_storage, target);
    if (set_err != ESP_OK)
    {
        s_mount_waiting = false;
        return set_err;
    }

    const esp_err_t wait_err = wait_mount_switch(target);
    if (wait_err != ESP_OK)
    {
        return wait_err;
    }

    if (target == TINYUSB_MSC_STORAGE_MOUNT_APP && !internal_vfs_ok())
    {
        ESP_LOGE(TAG, "/int 仍不可访问");
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t mount_wl_partition()
{
    if (s_wl != WL_INVALID_HANDLE)
    {
        return ESP_OK;
    }

    const esp_partition_t* part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                         ESP_PARTITION_SUBTYPE_DATA_FAT,
                                                         k_label_userdata);
    if (part == nullptr)
    {
        ESP_LOGE(TAG, "未找到 userdata FAT 分区");
        return ESP_ERR_NOT_FOUND;
    }

    const esp_err_t err = wl_mount(part, &s_wl);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "wl_mount 失败: %s", esp_err_to_name(err));
        s_wl = WL_INVALID_HANDLE;
    }
    return err;
}

} // namespace

esp_err_t usb_msc_init_internal()
{
    if (s_storage != nullptr)
    {
        return ESP_OK;
    }

    if (s_mount_sem == nullptr)
    {
        s_mount_sem = xSemaphoreCreateBinary();
        if (s_mount_sem == nullptr)
        {
            return ESP_ERR_NO_MEM;
        }
    }

    esp_err_t err = mount_wl_partition();
    if (err != ESP_OK)
    {
        return err;
    }

    tinyusb_msc_storage_config_t cfg{};
    cfg.medium.wl_handle = s_wl;
    cfg.mount_point      = TINYUSB_MSC_STORAGE_MOUNT_APP;
    cfg.fat_fs.base_path = s_base_path;
    cfg.fat_fs.config.max_files              = CONFIG_EBOOK_INTERNAL_MAX_FILES;
    cfg.fat_fs.config.format_if_mount_failed = CONFIG_EBOOK_STORAGE_FORMAT_IF_MOUNT_FAILED != 0;
    cfg.fat_fs.config.allocation_unit_size   = CONFIG_WL_SECTOR_SIZE;

    err = tinyusb_msc_new_storage_spiflash(&cfg, &s_storage);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "tinyusb_msc_new_storage_spiflash 失败: %s", esp_err_to_name(err));
        (void)wl_unmount(s_wl);
        s_wl = WL_INVALID_HANDLE;
        s_storage = nullptr;
        return err;
    }

    (void)tinyusb_msc_set_storage_callback(on_storage_event, nullptr);

    if (!s_tinyusb_installed)
    {
        const tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG(on_usb_device_event);
        err                           = tinyusb_driver_install(&tusb_cfg);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "tinyusb_driver_install 失败: %s", esp_err_to_name(err));
            (void)tinyusb_msc_delete_storage(s_storage);
            s_storage = nullptr;
            (void)wl_unmount(s_wl);
            s_wl = WL_INVALID_HANDLE;
            return err;
        }
        s_tinyusb_installed = true;
    }

    ESP_LOGI(TAG, "userdata MSC 已就绪, 挂载 %s", s_base_path);
    return ESP_OK;
}

esp_err_t usb_msc_expose_to_host()
{
    return switch_mount_point(TINYUSB_MSC_STORAGE_MOUNT_USB);
}

esp_err_t usb_msc_reclaim_for_app()
{
    esp_err_t err = switch_mount_point(TINYUSB_MSC_STORAGE_MOUNT_APP);
    if (err == ESP_OK)
    {
        return ESP_OK;
    }

    ESP_LOGW(TAG, "挂回失败, 尝试强制复位: %s", esp_err_to_name(err));
    (void)switch_mount_point(TINYUSB_MSC_STORAGE_MOUNT_USB);
    return switch_mount_point(TINYUSB_MSC_STORAGE_MOUNT_APP);
}

void usb_msc_service_recovery()
{
    if (s_storage == nullptr)
    {
        return;
    }

    if (internal_vfs_ok())
    {
        s_idle_streak          = 0;
        s_recovery_fail_streak = 0;
        return;
    }

    bool need_reclaim = false;

    if (usb_msc_is_exposed_to_host())
    {
        if (tud_mounted())
        {
            s_host_was_active = true;
        }

        if (s_host_was_active && (tud_suspended() || !tud_mounted()))
        {
            if (++s_idle_streak >= kIdleStreakReclaim)
            {
                ESP_LOGI(TAG, "USB 总线挂起, 判定已拔线, 自动挂回 /int");
                s_host_was_active = false;
                s_idle_streak     = 0;
                need_reclaim      = true;
            }
        }
        else
        {
            s_idle_streak = 0;
        }
    }
    else if (!internal_vfs_ok())
    {
        need_reclaim = true;
    }

    if (!need_reclaim || s_recovery_fail_streak >= kRecoveryMaxStreak)
    {
        return;
    }

    const esp_err_t err = usb_msc_reclaim_for_app();
    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "userdata 已恢复本机访问");
        s_recovery_fail_streak = 0;
        return;
    }

    ++s_recovery_fail_streak;
    ESP_LOGW(TAG, "userdata 挂回重试 %u/%u: %s",
             static_cast<unsigned>(s_recovery_fail_streak),
             static_cast<unsigned>(kRecoveryMaxStreak),
             esp_err_to_name(err));
}

bool usb_msc_is_exposed_to_host()
{
    if (s_storage == nullptr)
    {
        return false;
    }

    tinyusb_msc_mount_point_t mp = TINYUSB_MSC_STORAGE_MOUNT_APP;
    if (tinyusb_msc_get_storage_mount_point(s_storage, &mp) != ESP_OK)
    {
        return false;
    }
    return mp == TINYUSB_MSC_STORAGE_MOUNT_USB;
}

bool usb_msc_is_available_to_app()
{
    if (s_storage == nullptr)
    {
        return false;
    }
    if (usb_msc_is_exposed_to_host())
    {
        return false;
    }
    return internal_vfs_ok();
}

bool usb_msc_is_initialized()
{
    return s_storage != nullptr;
}

void usb_msc_shutdown()
{
    s_mount_waiting     = false;
    s_host_was_active   = false;
    s_idle_streak       = 0;
    s_recovery_fail_streak = 0;

    if (s_storage != nullptr)
    {
        (void)tinyusb_msc_delete_storage(s_storage);
        s_storage = nullptr;
    }

    if (s_tinyusb_installed)
    {
        (void)tinyusb_driver_uninstall();
        s_tinyusb_installed = false;
    }

    if (s_wl != WL_INVALID_HANDLE)
    {
        (void)wl_unmount(s_wl);
        s_wl = WL_INVALID_HANDLE;
    }
}

} // namespace app::common::storage

#endif
