#include "apps/reader/book_shelf.hpp"

#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

#include "core/log.hpp"
#include "storage/storage.hpp"
#include "text/path_encoding.hpp"

static const char* const TAG = "BookShelf";

namespace app::ebook::apps::reader {

namespace {

using Mount = ::app::common::storage::MountKind;

bool ends_with_txt_ci(const char* name)
{
    const size_t n = (name != nullptr) ? std::strlen(name) : 0;
    if (n < 4)
        return false;
    const char* t = name + n - 4;
    for (int i = 0; i < 4; ++i)
    {
        char c = t[i];
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c + ('a' - 'A'));
        if (".txt"[i] != c)
            return false;
    }
    return true;
}

void title_from_filename(const char* file_name, char* out, size_t out_cap)
{
    if (file_name == nullptr || out == nullptr || out_cap == 0)
        return;

    char raw[48];
    const size_t flen = std::strlen(file_name);
    size_t copy_len   = flen;
    if (flen >= 4 && ends_with_txt_ci(file_name))
        copy_len = flen - 4U;
    if (copy_len >= sizeof(raw))
        copy_len = sizeof(raw) - 1U;
    std::memcpy(raw, file_name, copy_len);
    raw[copy_len] = '\0';

    (void)text::normalize_path_segment(raw, out, out_cap);
    if (out[0] == '\0')
    {
        const size_t n = (copy_len < out_cap - 1) ? copy_len : (out_cap - 1);
        std::memcpy(out, raw, n);
        out[n] = '\0';
    }
}

} // namespace

void BookShelf::clear()
{
    count_ = 0;
}

const BookItem& BookShelf::item(uint8_t idx) const
{
    static const BookItem kEmpty{};
    return (idx < count_) ? items_[idx] : kEmpty;
}

bool BookShelf::add_unique(const char* full_path, const char* file_name, uint32_t size_bytes)
{
    if (count_ >= kMaxBooks || full_path == nullptr || file_name == nullptr)
        return false;
    if (!ends_with_txt_ci(file_name))
        return false;

    for (uint8_t i = 0; i < count_; ++i)
    {
        if (std::strcmp(items_[i].path, full_path) == 0)
            return false;
    }

    BookItem& b = items_[count_];
    (void)std::strncpy(b.path, full_path, sizeof(b.path) - 1);
    b.path[sizeof(b.path) - 1] = '\0';
    title_from_filename(file_name, b.title, sizeof(b.title));
    b.size_bytes = size_bytes;
    ++count_;
    return true;
}

bool BookShelf::scan_mount(const char* base_mount)
{
    if (base_mount == nullptr)
        return false;

    auto& sto = ::app::common::storage::StorageMgr::get_instance();
    const Mount kind = ::app::common::storage::mount_kind_from_path_prefix(base_mount);
    if (kind == Mount::Invalid || !sto.is_mounted(kind))
        return false;

    char dir_path[96];
    const int n = std::snprintf(dir_path, sizeof(dir_path), "%s%s", base_mount, kBookDirTxt);
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(dir_path))
        return false;

    DIR* dp = ::opendir(dir_path);
    if (dp == nullptr)
        return false;

    struct dirent* ent = nullptr;
    while ((ent = ::readdir(dp)) != nullptr && count_ < kMaxBooks)
    {
        const char* name = ent->d_name;
        if (name[0] == '\0' || name[0] == '.')
            continue;
        if (!ends_with_txt_ci(name))
            continue;

        char full[96];
        const int pn = std::snprintf(full, sizeof(full), "%s/%s", dir_path, name);
        if (pn <= 0 || static_cast<size_t>(pn) >= sizeof(full))
            continue;

        struct stat st{};
        if (::stat(full, &st) != 0 || S_ISDIR(st.st_mode))
            continue;

        (void)add_unique(full, name, static_cast<uint32_t>(st.st_size));
    }
    ::closedir(dp);
    return true;
}

void BookShelf::sort_by_title()
{
    for (uint8_t i = 1; i < count_; ++i)
    {
        BookItem tmp = items_[i];
        uint8_t j    = i;
        while (j > 0 && std::strcmp(items_[j - 1].title, tmp.title) > 0)
        {
            items_[j] = items_[j - 1];
            --j;
        }
        items_[j] = tmp;
    }
}

void BookShelf::scan()
{
    clear();
    (void)scan_mount(::app::common::storage::k_path_internal);
    (void)scan_mount(::app::common::storage::k_path_sd);
    if (count_ > 1)
        sort_by_title();
    EBOOK_LOGD(TAG, "found %u books", static_cast<unsigned>(count_));
}

} // namespace app::ebook::apps::reader
