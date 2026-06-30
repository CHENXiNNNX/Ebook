#pragma once

#include <cstddef>
#include <cstdint>

namespace app::ebook::apps::reader {

/** 书架条目 */
struct BookItem
{
    char     path[96]{};
    char     title[48]{};
    uint32_t size_bytes{0};
};

/**
 * @brief 书架扫描（仅 /int/Ebook/txt 与 /sd/Ebook/txt，非全盘检索）
 */
class BookShelf
{
  public:
    static constexpr uint8_t     kMaxBooks   = 48;
    static constexpr const char* kBookDirTxt = "/Ebook/txt";

    /** 与 ends_with_txt_ci() / scan_mount() 一致，供空状态提示 */
    static constexpr const char* kScanFmtHint  = "TXT";
    static constexpr const char* kScanPathInt  = "/int/Ebook/txt";
    static constexpr const char* kScanPathSd   = "/sd/Ebook/txt";

    void clear();
    void scan();

    uint8_t count() const { return count_; }
    const BookItem& item(uint8_t idx) const;

  private:
    bool scan_mount(const char* base_mount);
    bool add_unique(const char* full_path, const char* file_name, uint32_t size_bytes);
    void sort_by_title();

    BookItem items_[kMaxBooks]{};
    uint8_t  count_{0};
};

} // namespace app::ebook::apps::reader
