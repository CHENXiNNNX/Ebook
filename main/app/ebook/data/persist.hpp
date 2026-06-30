#pragma once

#include <cstddef>
#include <cstdint>

#include "core/result.hpp"

namespace app::ebook::data {

/**
 * @brief NVS 键值（namespace "ebook"；UI 任务内调用；set_* 不自动 commit）
 */
class Persist
{
  public:
    static core::Status init();
    static void deinit();

    static bool get_u8 (const char* key, uint8_t& out);
    static bool set_u8 (const char* key, uint8_t v);

    static bool get_bool(const char* key, bool& out);
    static bool set_bool(const char* key, bool v);

    static bool get_str (const char* key, char* out, size_t out_size);
    static bool set_str (const char* key, const char* v);

    static void commit();
};

} // namespace app::ebook::data
