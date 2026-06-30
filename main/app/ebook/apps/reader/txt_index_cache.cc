#include "apps/reader/txt_index_cache.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>

#include <sys/stat.h>

#include "core/log.hpp"
#include "storage/storage.hpp"

static const char* const TAG = "TxtIndexCache";

namespace app::ebook::apps::reader {

namespace {

constexpr uint32_t    kMagicIndex      = 0x54585449U;
constexpr uint32_t    kMagicProgress   = 0x50545854U;
constexpr uint8_t     kIndexVersion    = 4;
constexpr uint8_t     kProgressVersion = 1;
constexpr const char* kCacheRoot       = "/.ebook/cache";

#pragma pack(push, 1)
struct IndexHeader
{
    uint32_t magic;
    uint8_t  version;
    uint8_t  reserved0;
    uint16_t viewport_w;
    uint16_t viewport_h;
    uint8_t  lines_per_page;
    uint8_t  font_size;
    uint16_t line_height;
    uint16_t screen_w;
    uint16_t screen_h;
    uint32_t file_size;
    uint32_t page_count;
};

struct ProgressRecord
{
    uint32_t magic;
    uint8_t  version;
    uint16_t current_page;
    uint16_t total_pages;
};
#pragma pack(pop)

uint32_t fnv1a32(const char* s)
{
    uint32_t h = 2166136261U;
    if (s == nullptr)
        return h;
    for (; *s != '\0'; ++s)
    {
        h ^= static_cast<uint8_t>(*s);
        h *= 16777619U;
    }
    return h;
}

const char* mount_for(const char* book_path)
{
    using namespace ::app::common::storage;
    if (book_path == nullptr)
        return k_path_internal;
    if (std::strncmp(book_path, k_path_sd, std::strlen(k_path_sd)) == 0)
        return k_path_sd;
    return k_path_internal;
}

bool mkdir_p(const char* path)
{
    if (path == nullptr || path[0] == '\0')
        return false;
    char tmp[128];
    const size_t len = std::strlen(path);
    if (len >= sizeof(tmp))
        return false;
    std::memcpy(tmp, path, len + 1);

    for (size_t i = 1; i < len; ++i)
    {
        if (tmp[i] != '/')
            continue;
        tmp[i] = '\0';
        if (::mkdir(tmp, 0755) != 0 && errno != EEXIST)
            return false;
        tmp[i] = '/';
    }
    if (::mkdir(tmp, 0755) != 0 && errno != EEXIST)
        return false;
    return true;
}

bool header_matches(const IndexHeader& h, const TxtIndexLayout& l)
{
    return h.viewport_w == l.viewport_w && h.viewport_h == l.viewport_h &&
           h.lines_per_page == l.lines_per_page && h.font_size == l.font_size &&
           h.line_height == l.line_height && h.screen_w == l.screen_w &&
           h.screen_h == l.screen_h && h.file_size == l.file_size &&
           h.reserved0 == l.text_encoding;
}

bool read_exact(FILE* f, void* p, size_t n)
{
    return std::fread(p, 1, n, f) == n;
}

bool write_exact(FILE* f, const void* p, size_t n)
{
    return std::fwrite(p, 1, n, f) == n;
}

bool progress_path(const char* book_path, char* out, size_t out_len)
{
    char dir[128], idx[144];
    if (!TxtIndexCache::make_paths(book_path, dir, sizeof(dir), idx, sizeof(idx)))
        return false;
    const int n = std::snprintf(out, out_len, "%s/progress.bin", dir);
    return n > 0 && static_cast<size_t>(n) < out_len;
}

} // namespace

bool TxtIndexCache::make_paths(const char* book_path,
                               char* dir_out, size_t dir_len,
                               char* index_out, size_t index_len)
{
    if (book_path == nullptr || dir_out == nullptr || index_out == nullptr)
        return false;

    const uint32_t hash = fnv1a32(book_path);
    const int dn = std::snprintf(dir_out, dir_len, "%s%s/txt_%08lx",
                            mount_for(book_path), kCacheRoot,
                            static_cast<unsigned long>(hash));
    if (dn <= 0 || static_cast<size_t>(dn) >= dir_len)
        return false;

    const int in = std::snprintf(index_out, index_len, "%s/index.bin", dir_out);
    return in > 0 && static_cast<size_t>(in) < index_len;
}

bool TxtIndexCache::ensure_dir(const char* dir_path)
{
    return mkdir_p(dir_path);
}

bool TxtIndexCache::load(const char* book_path, const TxtIndexLayout& layout,
                         uint32_t* offsets, uint16_t* total_pages)
{
    if (book_path == nullptr || offsets == nullptr || total_pages == nullptr)
        return false;

    char dir[128], index_path[144];
    if (!make_paths(book_path, dir, sizeof(dir), index_path, sizeof(index_path)))
        return false;

    FILE* f = std::fopen(index_path, "rb");
    if (f == nullptr)
        return false;

    IndexHeader hdr{};
    if (!read_exact(f, &hdr, sizeof(hdr)))
    {
        std::fclose(f);
        return false;
    }
    if (hdr.magic != kMagicIndex || hdr.version != kIndexVersion ||
        !header_matches(hdr, layout))
    {
        std::fclose(f);
        return false;
    }
    if (hdr.page_count == 0 || hdr.page_count > kMaxPages)
    {
        std::fclose(f);
        return false;
    }

    for (uint32_t i = 0; i < hdr.page_count; ++i)
    {
        uint32_t off = 0;
        if (!read_exact(f, &off, sizeof(off)))
        {
            std::fclose(f);
            return false;
        }
        offsets[i] = off;
        if (i > 0 && off <= offsets[i - 1])
        {
            std::fclose(f);
            return false;
        }
    }
    std::fclose(f);

    *total_pages = static_cast<uint16_t>(hdr.page_count);
    EBOOK_LOGD(TAG, "cache hit %u pages", static_cast<unsigned>(*total_pages));
    return true;
}

bool TxtIndexCache::save(const char* book_path, const TxtIndexLayout& layout,
                         const uint32_t* offsets, uint16_t total_pages)
{
    if (book_path == nullptr || offsets == nullptr || total_pages == 0)
        return false;

    char dir[128], index_path[144];
    if (!make_paths(book_path, dir, sizeof(dir), index_path, sizeof(index_path)))
        return false;
    if (!ensure_dir(dir))
        return false;

    FILE* f = std::fopen(index_path, "wb");
    if (f == nullptr)
        return false;

    IndexHeader hdr{};
    hdr.magic         = kMagicIndex;
    hdr.version       = kIndexVersion;
    hdr.reserved0     = layout.text_encoding;
    hdr.viewport_w    = layout.viewport_w;
    hdr.viewport_h    = layout.viewport_h;
    hdr.lines_per_page = layout.lines_per_page;
    hdr.font_size     = layout.font_size;
    hdr.line_height   = layout.line_height;
    hdr.screen_w      = layout.screen_w;
    hdr.screen_h      = layout.screen_h;
    hdr.file_size     = layout.file_size;
    hdr.page_count    = total_pages;

    bool ok = write_exact(f, &hdr, sizeof(hdr));
    for (uint16_t i = 0; ok && i < total_pages; ++i)
        ok = write_exact(f, &offsets[i], sizeof(uint32_t));
    std::fclose(f);

    if (!ok)
    {
        (void)std::remove(index_path);
        return false;
    }
    return true;
}

bool TxtIndexCache::load_progress(const char* book_path, uint16_t* current_page)
{
    if (book_path == nullptr || current_page == nullptr)
        return false;
    char path[160];
    if (!progress_path(book_path, path, sizeof(path)))
        return false;

    FILE* f = std::fopen(path, "rb");
    if (f == nullptr)
        return false;

    ProgressRecord pf{};
    const bool ok = read_exact(f, &pf, sizeof(pf));
    std::fclose(f);
    if (!ok || pf.magic != kMagicProgress || pf.version != kProgressVersion ||
        pf.total_pages == 0)
        return false;

    *current_page = pf.current_page;
    if (*current_page >= pf.total_pages)
        *current_page = static_cast<uint16_t>(pf.total_pages - 1);
    return true;
}

bool TxtIndexCache::save_progress(const char* book_path, uint16_t current_page,
                                  uint16_t total_pages)
{
    if (book_path == nullptr || total_pages == 0)
        return false;

    char path[160], dir[128], index_path[144];
    if (!progress_path(book_path, path, sizeof(path)))
        return false;
    if (!make_paths(book_path, dir, sizeof(dir), index_path, sizeof(index_path)))
        return false;
    if (!ensure_dir(dir))
        return false;

    if (current_page >= total_pages)
        current_page = static_cast<uint16_t>(total_pages - 1);

    ProgressRecord pf{};
    pf.magic         = kMagicProgress;
    pf.version       = kProgressVersion;
    pf.current_page  = current_page;
    pf.total_pages   = total_pages;

    FILE* f = std::fopen(path, "wb");
    if (f == nullptr)
        return false;
    const bool ok = write_exact(f, &pf, sizeof(pf));
    std::fclose(f);
    return ok;
}

bool TxtIndexCache::query_read_percent(const char* book_path, uint8_t* percent_out)
{
    if (book_path == nullptr || percent_out == nullptr)
        return false;
    char path[160];
    if (!progress_path(book_path, path, sizeof(path)))
        return false;

    FILE* f = std::fopen(path, "rb");
    if (f == nullptr)
        return false;

    ProgressRecord pf{};
    const bool ok = read_exact(f, &pf, sizeof(pf));
    std::fclose(f);
    if (!ok || pf.magic != kMagicProgress || pf.version != kProgressVersion ||
        pf.total_pages == 0)
        return false;

    const uint32_t pct =
        (static_cast<uint32_t>(pf.current_page + 1U) * 100U) /
        static_cast<uint32_t>(pf.total_pages);
    *percent_out = static_cast<uint8_t>(pct > 100U ? 100U : pct);
    return true;
}

} // namespace app::ebook::apps::reader
