#pragma once

#include <cstddef>
#include <cstdint>

namespace app::ebook::apps::reader {

/**
 * @brief TXT 分页索引磁盘缓存
 *
 * 路径：{mount}/.ebook/cache/txt_{fnv1a32(path):08x}/
 *   - index.bin    分页偏移（"TXTI" v4）
 *   - progress.bin 阅读进度（"TXTP" v1）
 */
struct TxtIndexLayout
{
    uint16_t viewport_w{0};
    uint16_t viewport_h{0};
    uint8_t  lines_per_page{0};
    uint8_t  font_size{0};
    uint16_t line_height{0};
    uint16_t screen_w{0};
    uint16_t screen_h{0};
    uint32_t file_size{0};
    uint8_t  text_encoding{0};
};

class TxtIndexCache
{
  public:
    static constexpr uint16_t kMaxPages = 8000;

    static bool make_paths(const char* book_path,
                           char* dir_out, size_t dir_len,
                           char* index_out, size_t index_len);

    static bool ensure_dir(const char* dir_path);

    static bool load(const char* book_path, const TxtIndexLayout& layout,
                     uint32_t* offsets, uint16_t* total_pages);

    static bool save(const char* book_path, const TxtIndexLayout& layout,
                     const uint32_t* offsets, uint16_t total_pages);

    static bool load_progress(const char* book_path, uint16_t* current_page);
    static bool save_progress(const char* book_path, uint16_t current_page,
                              uint16_t total_pages);

    static bool query_read_percent(const char* book_path, uint8_t* percent_out);
};

} // namespace app::ebook::apps::reader
