#pragma once

#include <cstdint>

namespace app::ebook::apps::reader {

/**
 * @brief 阅读进度（NVS: bk.*；每书页码见 TxtIndexCache::progress.bin）
 */
class ReadingStore
{
  public:
    static ReadingStore& get_instance();

    void load();

    bool        has_book()    const { return has_book_; }
    const char* title()       const { return title_; }
    const char* book_path()   const { return path_; }
    uint16_t    current_page() const { return current_page_; }
    uint16_t    total_pages()  const { return total_pages_; }

    /** 已读百分比 1..100 */
    uint8_t percent() const
    {
        if (!has_book_ || total_pages_ == 0) return 0;
        const uint32_t pct =
            (static_cast<uint32_t>(current_page_ + 1U) * 100U) /
            static_cast<uint32_t>(total_pages_);
        return static_cast<uint8_t>(pct > 100U ? 100U : pct);
    }

    void set_book(const char* title, const char* path,
                  uint16_t current, uint16_t total);
    void clear();

  private:
    ReadingStore() = default;

    static constexpr size_t kTitleMax = 48;
    static constexpr size_t kPathMax  = 96;

    bool     has_book_{false};
    char     title_[kTitleMax + 1]{};
    char     path_[kPathMax + 1]{};
    uint16_t current_page_{0};
    uint16_t total_pages_{0};
};

} // namespace app::ebook::apps::reader
