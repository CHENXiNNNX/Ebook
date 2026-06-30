#include "apps/reader/reading_store.hpp"

#include <cstring>

#include "data/persist.hpp"

namespace app::ebook::apps::reader {

namespace {

constexpr const char* kKTitle = "bk.title";
constexpr const char* kKPath  = "bk.path";
constexpr const char* kKCurLo = "bk.cur";
constexpr const char* kKCurHi = "bk.curh";
constexpr const char* kKTotLo = "bk.tot";
constexpr const char* kKTotHi = "bk.toth";

uint16_t load_u16(const char* lo_key, const char* hi_key)
{
    uint8_t lo = 0, hi = 0;
    if (!data::Persist::get_u8(lo_key, lo)) return 0;
    (void)data::Persist::get_u8(hi_key, hi);  // 高字节可缺失
    return static_cast<uint16_t>((hi << 8) | lo);
}

void store_u16(const char* lo_key, const char* hi_key, uint16_t v)
{
    data::Persist::set_u8(lo_key, static_cast<uint8_t>(v & 0xFF));
    data::Persist::set_u8(hi_key, static_cast<uint8_t>((v >> 8) & 0xFF));
}

void copy_clamped(char* dst, size_t cap, const char* src)
{
    if (src == nullptr || cap == 0) { if (cap > 0) dst[0] = '\0'; return; }
    std::strncpy(dst, src, cap - 1);
    dst[cap - 1] = '\0';
}

} // namespace

ReadingStore& ReadingStore::get_instance()
{
    static ReadingStore s;
    return s;
}

void ReadingStore::load()
{
    has_book_ = false;
    title_[0] = '\0';
    path_[0]  = '\0';

    char buf[kPathMax + 1] = {};

    if (data::Persist::get_str(kKTitle, buf, sizeof(buf)) && buf[0] != '\0')
    {
        copy_clamped(title_, sizeof(title_), buf);
        has_book_ = true;
    }

    buf[0] = '\0';
    if (data::Persist::get_str(kKPath, buf, sizeof(buf)) && buf[0] != '\0')
        copy_clamped(path_, sizeof(path_), buf);

    current_page_ = load_u16(kKCurLo, kKCurHi);
    total_pages_  = load_u16(kKTotLo, kKTotHi);
}

void ReadingStore::set_book(const char* title, const char* path,
                            uint16_t current, uint16_t total)
{
    if (title == nullptr) { clear(); return; }

    copy_clamped(title_, sizeof(title_), title);
    copy_clamped(path_,  sizeof(path_),  path);
    current_page_ = current;
    total_pages_  = total;
    has_book_     = (title_[0] != '\0');

    data::Persist::set_str(kKTitle, title_);
    data::Persist::set_str(kKPath,  path_);
    store_u16(kKCurLo, kKCurHi, current);
    store_u16(kKTotLo, kKTotHi, total);
    data::Persist::commit();
}

void ReadingStore::clear()
{
    has_book_    = false;
    title_[0]    = '\0';
    path_[0]     = '\0';
    current_page_ = 0;
    total_pages_  = 0;
}

} // namespace app::ebook::apps::reader
