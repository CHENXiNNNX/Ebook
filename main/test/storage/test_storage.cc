#include "test_storage.hpp"

#include "storage.hpp"
#include "system/task/task.hpp"
#include "test_log.hpp"

#include <cstdio>
#include <cstring>
#include <inttypes.h>

#include "esp_log.h"
#include "sdkconfig.h"

#ifndef CONFIG_EBOOK_SD_SDMMC_ENABLE
#define CONFIG_EBOOK_SD_SDMMC_ENABLE 0
#endif
#ifndef CONFIG_EBOOK_SD_HOTPLUG_SERVICE_MS
#define CONFIG_EBOOK_SD_HOTPLUG_SERVICE_MS 500
#endif

namespace {

const char* const TAG = "test_storage";

constexpr const char* k_rel_assets_font = "/fonts/simhei_12.bin";

constexpr const char* k_rel_int_file = "/int_smoke.txt";
constexpr const char* k_payload_int = "Ebook /int FAT smoke\n";

constexpr const char* k_rel_sd_file = "/sd_smoke.txt";
constexpr const char* k_payload_sd = "Ebook /sd FAT smoke\n";

using Storage = app::common::storage::StorageMgr;
using MK = app::common::storage::MountKind;

Storage& storage()
{
    return Storage::get_instance();
}

void log_assets_usage(const char* when_zh)
{
    size_t total = 0;
    size_t used = 0;
    const esp_err_t err = storage().query_assets_usage(&total, &used);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "%s: 查询 assets LittleFS 用量失败 (%s)", when_zh, esp_err_to_name(err));
        return;
    }
    app::test::log_kv_fmt(TAG, when_zh, "总计 %u 字节, 已用 %u 字节", static_cast<unsigned>(total),
                          static_cast<unsigned>(used));
}

void log_internal_usage(const char* when_zh)
{
    size_t total = 0;
    size_t used = 0;
    const esp_err_t err = storage().query_internal_usage(&total, &used);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "%s: 查询 LittleFS 用量失败 (%s)", when_zh, esp_err_to_name(err));
        return;
    }
    app::test::log_kv_fmt(TAG, when_zh, "总计 %u 字节, 已用 %u 字节", static_cast<unsigned>(total),
                          static_cast<unsigned>(used));
}

void log_sd_usage(const char* when_zh)
{
    uint64_t total = 0;
    uint64_t used = 0;
    const esp_err_t err = storage().query_sd_usage(&total, &used);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "%s: query_sd_usage 失败 (%s)", when_zh, esp_err_to_name(err));
        return;
    }
    app::test::log_kv_fmt(TAG, when_zh, "总计 %" PRIu64 " 字节, 已用 %" PRIu64 " 字节", total,
                          used);
}

bool write_read_file(const char* path, const char* payload, const char* check_name)
{
    FILE* wf = std::fopen(path, "w");
    if (wf == nullptr)
    {
        ESP_LOGE(TAG, "打开文件失败 (写): %s", path);
        return false;
    }

    const size_t payload_len = std::strlen(payload);
    const size_t nw = std::fwrite(payload, 1U, payload_len, wf);
    const int flush_rc = std::fflush(wf);
    const int close_w = std::fclose(wf);
    if (nw != payload_len || flush_rc != 0 || close_w != 0)
    {
        ESP_LOGE(TAG, "写入失败: 字节=%u fflush=%d fclose=%d", static_cast<unsigned>(nw),
                 flush_rc, close_w);
        return false;
    }
    app::test::log_kv_fmt(TAG, "已写入", "%u 字节 -> %s", static_cast<unsigned>(nw), path);

    FILE* rf = std::fopen(path, "r");
    if (rf == nullptr)
    {
        ESP_LOGE(TAG, "打开文件失败 (读): %s", path);
        return false;
    }

    char buf[64];
    const size_t nr = std::fread(buf, 1U, sizeof(buf) - 1U, rf);
    buf[nr] = '\0';
    (void)std::fclose(rf);

    const bool match = (nr == payload_len && std::strcmp(buf, payload) == 0);
    app::test::log_kv(TAG, "读回", match ? "与原文一致" : "不一致");
    if (!match)
    {
        ESP_LOGE(TAG, "内容与预期不符 (实际长度 %u)", static_cast<unsigned>(std::strlen(buf)));
    }
    app::test::log_check(TAG, check_name, match);
    return match;
}

bool path_smoke(void)
{
    app::test::log_section_begin(TAG, "[路径] 前缀与 MountKind");
    using app::common::storage::mount_kind_from_path_prefix;

    const MK a = mount_kind_from_path_prefix("/assets/x");
    const MK i = mount_kind_from_path_prefix("/int");
    const MK s = mount_kind_from_path_prefix("/sd");
    const MK u = mount_kind_from_path_prefix("/usb");
    const MK bad_rel = mount_kind_from_path_prefix("assets/foo");
    const MK bad = mount_kind_from_path_prefix("/unknown");

    app::test::log_kv_fmt(TAG, "/assets/x", "实际=%d 期望=%d", static_cast<int>(a),
                          static_cast<int>(MK::Assets));
    app::test::log_kv_fmt(TAG, "/int", "实际=%d 期望=%d", static_cast<int>(i),
                          static_cast<int>(MK::Internal));
    app::test::log_kv_fmt(TAG, "/sd", "实际=%d 期望=%d", static_cast<int>(s),
                          static_cast<int>(MK::Sd));
    app::test::log_kv_fmt(TAG, "/usb", "实际=%d 期望=%d", static_cast<int>(u),
                          static_cast<int>(MK::Usb));
    app::test::log_kv_fmt(TAG, "assets/foo", "实际=%d 期望=%d (无效)", static_cast<int>(bad_rel),
                          static_cast<int>(MK::Invalid));
    app::test::log_kv_fmt(TAG, "/unknown", "实际=%d 期望=%d (无效)", static_cast<int>(bad),
                          static_cast<int>(MK::Invalid));

    const bool ok = (a == MK::Assets && i == MK::Internal && s == MK::Sd && u == MK::Usb &&
                     bad_rel == MK::Invalid && bad == MK::Invalid);
    app::test::log_check(TAG, "路径前缀推断", ok);
    app::test::log_section_end(TAG);
    return ok;
}

void mount_volume_apis_smoke(void)
{
    app::test::log_section_begin(TAG, "[卷] 挂载 API");

    esp_err_t e = storage().mount_internal();
    app::test::log_kv(TAG, "再次 mount_internal (幂等)", esp_err_to_name(e));

#if CONFIG_EBOOK_SD_SDMMC_ENABLE
    e = storage().mount_sd();
    app::test::log_kv(TAG, "TF mount_sd", esp_err_to_name(e));
#else
    app::test::log_kv(TAG, "TF mount_sd", "已跳过 (EBOOK_SD_SDMMC_ENABLE=n)");
#endif

    e = storage().mount_usb();
    app::test::log_kv(TAG, "U 盘 mount_usb", esp_err_to_name(e));
    app::test::log_section_end(TAG);
}

bool write_read_internal_smoke(void)
{
    app::test::log_section_begin(TAG, "[读写] /int (FAT)");

    if (!storage().is_mounted(MK::Internal))
    {
        ESP_LOGE(TAG, "/int 未挂载");
        app::test::log_section_end(TAG);
        return false;
    }

    char path[64];
    std::snprintf(path, sizeof(path), "%s%s", app::common::storage::k_path_internal,
                  k_rel_int_file);

    log_internal_usage("写入前");
    const bool ok = write_read_file(path, k_payload_int, "/int 读写一致性");
    log_internal_usage("写入后");
    app::test::log_section_end(TAG);
    return ok;
}

bool write_read_sd_smoke(void)
{
    app::test::log_section_begin(TAG, "[读写] /sd (TF FAT)");

#if !CONFIG_EBOOK_SD_SDMMC_ENABLE
    app::test::log_kv(TAG, "SD FAT 写读", "已跳过 (EBOOK_SD_SDMMC_ENABLE=n)");
    app::test::log_check(TAG, "/sd 写读 (不适用)", true);
    app::test::log_section_end(TAG);
    return true;
#endif

    if (!storage().is_mounted(MK::Sd))
    {
        ESP_LOGW(TAG, "/sd 未挂载");
        app::test::log_check(TAG, "/sd 写读", false);
        app::test::log_section_end(TAG);
        return false;
    }

    char path[64];
    std::snprintf(path, sizeof(path), "%s%s", app::common::storage::k_path_sd, k_rel_sd_file);

    log_sd_usage("写入前");
    const bool ok = write_read_file(path, k_payload_sd, "/sd FAT 读写一致性");
    log_sd_usage("写入后");
    app::test::log_section_end(TAG);
    return ok;
}

bool service_sd_hotplug_smoke(void)
{
    app::test::log_section_begin(TAG, "[热插拔] service_sd_hotplug");

#if !CONFIG_EBOOK_SD_SDMMC_ENABLE
    app::test::log_kv(TAG, "service_sd_hotplug", "已跳过 (EBOOK_SD_SDMMC_ENABLE=n)");
    app::test::log_check(TAG, "TF 热插拔 (不适用)", true);
    app::test::log_section_end(TAG);
    return true;
#else
    app::test::log_kv_fmt(TAG, "轮询间隔", "%d ms", CONFIG_EBOOK_SD_HOTPLUG_SERVICE_MS);

    if (!storage().is_mounted(MK::Sd))
    {
        ESP_LOGW(TAG, "/sd 未挂载, 跳过热插拔再挂载检查");
        app::test::log_check(TAG, "service_sd_hotplug (本趟未挂 TF)", true);
        app::test::log_section_end(TAG);
        return true;
    }

    const esp_err_t uerr = storage().umount_sd();
    app::test::log_kv(TAG, "umount_sd", esp_err_to_name(uerr));
    if (uerr != ESP_OK || storage().is_mounted(MK::Sd))
    {
        app::test::log_check(TAG, "卸载 /sd", false);
        app::test::log_section_end(TAG);
        return false;
    }

    app::sys::task::TaskMgr::delay_ms(static_cast<uint32_t>(CONFIG_EBOOK_SD_HOTPLUG_SERVICE_MS) +
                                      200U);
    storage().service_sd_hotplug();

    const bool mounted_again = storage().is_mounted(MK::Sd);
    app::test::log_kv(TAG, "service 后 /sd", mounted_again ? "已挂载" : "未挂载");
    app::test::log_check(TAG, "service_sd_hotplug 再挂载", mounted_again);
    app::test::log_section_end(TAG);
    return mounted_again;
#endif
}

bool read_only_assets_smoke(void)
{
    app::test::log_section_begin(TAG, "[只读] /assets (LittleFS)");

    char path[64];
    std::snprintf(path, sizeof(path), "%s%s", app::common::storage::k_path_assets,
                  k_rel_assets_font);

    log_assets_usage("挂载后");

    FILE* rf = std::fopen(path, "rb");
    if (rf == nullptr)
    {
        ESP_LOGE(TAG, "无法读取系统字库: %s", path);
        app::test::log_section_end(TAG);
        return false;
    }
    const int ch = std::fgetc(rf);
    (void)std::fclose(rf);
    app::test::log_check(TAG, "系统字库可读", ch != EOF);

    std::snprintf(path, sizeof(path), "%s/storage_smoke.txt",
                  app::common::storage::k_path_assets);
    FILE* wf = std::fopen(path, "w");
    const bool write_blocked = (wf == nullptr);
    if (wf != nullptr)
        (void)std::fclose(wf);
    app::test::log_check(TAG, "系统分区拒绝写入", write_blocked);

    app::test::log_section_end(TAG);
    return (ch != EOF) && write_blocked;
}

bool umount_remount_assets_smoke(const char* font_path)
{
    app::test::log_section_begin(TAG, "[生命周期] assets 卸载再挂载");

    esp_err_t err = storage().umount_assets();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "umount_assets: %s", esp_err_to_name(err));
        app::test::log_section_end(TAG);
        return false;
    }

    app::test::log_kv(TAG, "卸载后 assets 已挂载",
                      storage().is_mounted(MK::Assets) ? "是 (异常)" : "否 (符合预期)");

    FILE* rf = std::fopen(font_path, "rb");
    if (rf != nullptr)
    {
        (void)std::fclose(rf);
        ESP_LOGE(TAG, "卸载后仍能打开文件");
    }
    else
    {
        app::test::log_kv(TAG, "卸载后读取", "打开失败 (符合预期)");
    }

    err = storage().mount_assets();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "mount_assets: %s", esp_err_to_name(err));
        app::test::log_section_end(TAG);
        return false;
    }

    rf = std::fopen(font_path, "rb");
    if (rf == nullptr)
    {
        ESP_LOGE(TAG, "再挂载后无法读取: %s", font_path);
        app::test::log_section_end(TAG);
        return false;
    }
    const int ch = std::fgetc(rf);
    (void)std::fclose(rf);

    const bool match = (ch != EOF);
    app::test::log_check(TAG, "再挂载后读回系统字库", match);
    log_assets_usage("再挂载后");
    app::test::log_section_end(TAG);
    return match;
}

} // namespace

extern "C" void test_storage(void)
{
    app::test::log_section_begin(TAG, "StorageMgr 功能测试 · 开始");

    esp_err_t err = storage().init_defaults();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "init_defaults: %s", esp_err_to_name(err));
        app::test::log_section_end(TAG);
        return;
    }

    app::test::log_kv(TAG, "assets 已挂载", storage().is_mounted(MK::Assets) ? "是" : "否");
    app::test::log_kv(TAG, "/int 已挂载", storage().is_mounted(MK::Internal) ? "是" : "否");
    log_internal_usage("init 后");
    app::test::log_section_end(TAG);

    (void)path_smoke();
    mount_volume_apis_smoke();

    const bool int_ok = write_read_internal_smoke();
    const bool sd_ok = write_read_sd_smoke();
    const bool hotplug_ok = service_sd_hotplug_smoke();
    const bool assets_ok = read_only_assets_smoke();

    char font_path[64];
    std::snprintf(font_path, sizeof(font_path), "%s%s", app::common::storage::k_path_assets,
                  k_rel_assets_font);
    const bool cycle_ok = assets_ok ? umount_remount_assets_smoke(font_path) : false;

    app::test::log_section_begin(TAG, "StorageMgr 功能测试 · 汇总");
    app::test::log_check(TAG, "/int 读写", int_ok);
#if CONFIG_EBOOK_SD_SDMMC_ENABLE
    app::test::log_check(TAG, "/sd 写读", sd_ok);
    app::test::log_check(TAG, "/sd 热插拔 service", hotplug_ok);
#else
    app::test::log_check(TAG, "/sd (SDMMC 关闭)", sd_ok);
    app::test::log_check(TAG, "/sd 热插拔 (关闭)", hotplug_ok);
#endif
    app::test::log_check(TAG, "/assets 只读", assets_ok);
    app::test::log_check(TAG, "/assets 卸载再挂载", cycle_ok);
    app::test::log_check(TAG, "总评", int_ok && sd_ok && hotplug_ok && assets_ok && cycle_ok);
    app::test::log_section_end(TAG);

    app::test::log_section_begin(TAG, "StorageMgr 功能测试 · 结束");
    app::test::log_section_end(TAG);
}
