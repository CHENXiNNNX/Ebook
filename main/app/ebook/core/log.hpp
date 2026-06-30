#pragma once

/** @brief ebook 日志门面（EBOOK_LOG*；粒度由 EBOOK_LOG_LEVEL 编译期裁剪） */

#include <esp_log.h>

#ifndef EBOOK_LOG_LEVEL
#define EBOOK_LOG_LEVEL ESP_LOG_INFO
#endif

#if EBOOK_LOG_LEVEL >= ESP_LOG_ERROR
#define EBOOK_LOGE(tag, fmt, ...) ESP_LOGE(tag, fmt, ##__VA_ARGS__)
#else
#define EBOOK_LOGE(tag, fmt, ...) ((void)0)
#endif

#if EBOOK_LOG_LEVEL >= ESP_LOG_WARN
#define EBOOK_LOGW(tag, fmt, ...) ESP_LOGW(tag, fmt, ##__VA_ARGS__)
#else
#define EBOOK_LOGW(tag, fmt, ...) ((void)0)
#endif

#if EBOOK_LOG_LEVEL >= ESP_LOG_INFO
#define EBOOK_LOGI(tag, fmt, ...) ESP_LOGI(tag, fmt, ##__VA_ARGS__)
#else
#define EBOOK_LOGI(tag, fmt, ...) ((void)0)
#endif

#if EBOOK_LOG_LEVEL >= ESP_LOG_DEBUG
#define EBOOK_LOGD(tag, fmt, ...) ESP_LOGD(tag, fmt, ##__VA_ARGS__)
#else
#define EBOOK_LOGD(tag, fmt, ...) ((void)0)
#endif

#if EBOOK_LOG_LEVEL >= ESP_LOG_VERBOSE
#define EBOOK_LOGV(tag, fmt, ...) ESP_LOGV(tag, fmt, ##__VA_ARGS__)
#else
#define EBOOK_LOGV(tag, fmt, ...) ((void)0)
#endif
