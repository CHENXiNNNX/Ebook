#pragma once

#include <cstdint>
#include <string>

namespace app::common::time {

/** @brief Unix 时间戳（秒），需 NTP 同步后才有意义 */
int64_t unix_timestamp_sec();

/** @brief Unix 时间戳（毫秒） */
int64_t unix_timestamp_ms();

/** @brief 自启动以来的毫秒数（重启归零） */
int64_t uptime_ms();

/** @brief 自启动以来的微秒数 */
int64_t uptime_us();

/** @brief 自启动以来的秒数 */
int64_t uptime_sec();

/** @brief ISO 8601 UTC 字符串，如 2025-03-12T19:00:00Z */
std::string iso8601_timestamp();

} // namespace app::common::time
