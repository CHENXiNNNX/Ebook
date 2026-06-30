#pragma once

#include <cstdarg>
#include <cstdio>

#include "esp_log.h"

namespace app::test {

/** @brief 功能测试统一日志：中文标题 + 分隔线 */
inline void log_section_begin(const char* tag, const char* title_zh)
{
    ESP_LOGI(tag, "");
    ESP_LOGI(tag, "【%s】", title_zh);
    ESP_LOGI(tag, "------------------------------------------------------------");
}

/** @brief 阶段收尾分隔线 */
inline void log_section_end(const char* tag)
{
    ESP_LOGI(tag, "------------------------------------------------------------");
}

inline void log_kv(const char* tag, const char* key_zh, const char* val)
{
    ESP_LOGI(tag, "  · %s： %s", key_zh, val ? val : "");
}

inline void log_kv_fmt(const char* tag, const char* key_zh, const char* fmt, ...)
{
    char buf[192];
    va_list ap{};
    va_start(ap, fmt);
    (void)vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    log_kv(tag, key_zh, buf);
}

/** @brief 断言式结果一行 */
inline void log_check(const char* tag, const char* name_zh, bool ok)
{
    ESP_LOGI(tag, "  ⇒ %s： %s", name_zh, ok ? "通过" : "未通过");
}

} // namespace app::test
