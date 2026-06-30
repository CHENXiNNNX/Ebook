#pragma once

#include "esp_err.h"
#include "sdkconfig.h"

namespace app::common::storage {

#if CONFIG_TINYUSB_MSC_ENABLED

/** 初始化 userdata FAT + TinyUSB MSC，默认挂载给应用 (/int) */
esp_err_t usb_msc_init_internal();

/** 将 userdata 暴露给 USB 主机（PC）；应用不可再访问 /int */
esp_err_t usb_msc_expose_to_host();

/** 从 PC 收回 userdata，恢复应用访问 /int */
esp_err_t usb_msc_reclaim_for_app();

bool usb_msc_is_exposed_to_host();

/** 应用侧是否可实际访问 /int（已挂回且 VFS 可用） */
bool usb_msc_is_available_to_app();

bool usb_msc_is_initialized();

void usb_msc_shutdown();

/** 周期调用：直接拔线未安全弹出时自动挂回 /int */
void usb_msc_service_recovery();

#endif

} // namespace app::common::storage
