#pragma once

#include <cstdint>

#include "apps/reader/reader_layout.hpp"
#include "apps/reader/txt_index_cache.hpp"
#include "gfx/canvas.hpp"

namespace app::ebook::apps::reader {

/** @brief 单本 TXT：加载 → 分页（可缓存）→ 按页绘制 */
class TxtBook
{
  public:
    static constexpr uint32_t kMaxFileBytes = 384U * 1024U;
    static constexpr uint16_t kMaxPages =
        static_cast<uint16_t>(TxtIndexCache::kMaxPages);

    bool open(const char* path, const ReaderLayout& layout);
    void close();

    bool set_layout(const ReaderLayout& layout);

    bool     is_open() const { return text_ != nullptr; }
    uint16_t page_count() const { return page_count_; }
    uint16_t page() const { return page_; }
    uint8_t  text_encoding() const { return text_encoding_; }
    uint32_t file_size() const { return file_size_; }

    bool set_page(uint16_t p);
    bool next_page();
    bool prev_page();

    void paint_page(gfx::Canvas& canvas, const core::Rect& area) const;

  private:
    bool load_file(const char* path);
    bool build_pages(const ReaderLayout& layout);
    bool try_load_cache(const ReaderLayout& layout);
    void save_cache(const ReaderLayout& layout);
    uint32_t skip_line(uint32_t offset, const ReaderLayout& layout) const;

    char*     text_{nullptr};
    uint32_t  text_len_{0};
    uint32_t* page_offsets_{nullptr};
    uint16_t  page_count_{0};
    uint16_t  page_{0};
    uint8_t   text_encoding_{0};
    uint32_t  file_size_{0};
    char      path_[96]{};
    ReaderLayout layout_{12};
};

} // namespace app::ebook::apps::reader
