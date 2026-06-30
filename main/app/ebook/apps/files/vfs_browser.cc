#include "apps/files/vfs_browser.hpp"

#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "core/log.hpp"

static const char* const TAG = "VfsBrowser";

namespace app::ebook::apps::files {

namespace {

using Mount = ::app::common::storage::MountKind;

constexpr const char* kSdLabel = "SD Card";

constexpr VfsBrowser::RootVol kRoots[] = {
    {::app::common::storage::k_path_internal, ::app::common::storage::k_label_userdata,
     Mount::Internal},
    {::app::common::storage::k_path_sd, kSdLabel, Mount::Sd},
    {::app::common::storage::k_path_assets, ::app::common::storage::k_label_assets,
     Mount::Assets},
};
constexpr uint8_t kRootSlotCount = sizeof(kRoots) / sizeof(kRoots[0]);

void sort_entries(VfsBrowser::Entry* entries, uint8_t count)
{
    for (uint8_t i = 1; i < count; ++i)
    {
        VfsBrowser::Entry tmp = entries[i];
        uint8_t           j   = i;
        while (j > 0)
        {
            const VfsBrowser::Entry& prev = entries[j - 1];
            bool                     before = false;
            if (prev.is_dir != tmp.is_dir)
                before = (!prev.is_dir && tmp.is_dir);
            else if (std::strcmp(prev.name, tmp.name) > 0)
                before = true;
            if (!before)
                break;
            entries[j] = prev;
            --j;
        }
        entries[j] = tmp;
    }
}

} // namespace

bool VfsBrowser::path_allowed(const char* path)
{
    if (path == nullptr || path[0] != '/')
        return false;
    const Mount kind = ::app::common::storage::mount_kind_from_path_prefix(path);
    return kind == Mount::Internal || kind == Mount::Sd || kind == Mount::Assets;
}

bool VfsBrowser::is_storage_root(const char* path)
{
    if (path == nullptr)
        return false;
    return std::strcmp(path, ::app::common::storage::k_path_internal) == 0 ||
           std::strcmp(path, ::app::common::storage::k_path_sd) == 0 ||
           std::strcmp(path, ::app::common::storage::k_path_assets) == 0;
}

uint8_t VfsBrowser::visible_root_count()
{
    auto& sto = ::app::common::storage::StorageMgr::get_instance();
    uint8_t n = 0;
    for (uint8_t i = 0; i < kRootSlotCount; ++i)
        if (sto.is_mounted(kRoots[i].kind))
            ++n;
    return n;
}

const VfsBrowser::RootVol* VfsBrowser::root_at_visible(uint8_t vis_idx)
{
    auto& sto = ::app::common::storage::StorageMgr::get_instance();
    uint8_t seen = 0;
    for (uint8_t i = 0; i < kRootSlotCount; ++i)
    {
        if (!sto.is_mounted(kRoots[i].kind))
            continue;
        if (seen == vis_idx)
            return &kRoots[i];
        ++seen;
    }
    return nullptr;
}

bool VfsBrowser::entry_hidden(const char* cwd, const char* name)
{
    if (name == nullptr)
        return true;

    static const char* const kHidden[] = {
        "System Volume Information", "$RECYCLE.BIN",
    };
    const bool on_sd = ::app::common::storage::path_has_prefix(cwd, ::app::common::storage::k_path_sd);
    const bool on_int =
        ::app::common::storage::path_has_prefix(cwd, ::app::common::storage::k_path_internal);
    if (!on_sd && !on_int)
        return false;

    for (const char* h : kHidden)
        if (std::strcmp(name, h) == 0)
            return true;
    return false;
}

const VfsBrowser::Entry& VfsBrowser::entry_at(uint8_t idx) const
{
    static const Entry kEmpty{};
    if (idx >= entry_count_)
        return kEmpty;
    return entries_[idx];
}

uint8_t VfsBrowser::row_count() const
{
    if (mode_ == Mode::Roots)
        return visible_root_count();
    return static_cast<uint8_t>(entry_count_ + (has_parent_row_ ? 1U : 0U));
}

void VfsBrowser::show_roots()
{
    mode_            = Mode::Roots;
    cwd_[0]          = '\0';
    browse_title_[0] = '\0';
    clear_entries();
}

void VfsBrowser::clear_entries()
{
    entry_count_    = 0;
    has_parent_row_ = false;
    truncated_      = false;
}

void VfsBrowser::sync_parent_row()
{
    has_parent_row_ = !is_storage_root(cwd_);
}

void VfsBrowser::sync_title_from_cwd()
{
    if (is_storage_root(cwd_))
    {
        for (uint8_t i = 0; i < kRootSlotCount; ++i)
        {
            if (std::strcmp(cwd_, kRoots[i].path) == 0)
            {
                std::strncpy(browse_title_, kRoots[i].label, sizeof(browse_title_) - 1);
                browse_title_[sizeof(browse_title_) - 1] = '\0';
                return;
            }
        }
    }

    const char* name = std::strrchr(cwd_, '/');
    name             = (name != nullptr) ? (name + 1) : cwd_;
    std::strncpy(browse_title_, name, sizeof(browse_title_) - 1);
    browse_title_[sizeof(browse_title_) - 1] = '\0';
}

bool VfsBrowser::scan_entries(const char* dir, Entry* out, uint8_t& out_count, bool& truncated_out)
{
    out_count      = 0;
    truncated_out  = false;
    if (dir == nullptr || out == nullptr)
        return false;

    struct stat dst{};
    if (stat(dir, &dst) != 0 || !S_ISDIR(dst.st_mode))
        return false;

    DIR* d = ::opendir(dir);
    if (d == nullptr)
        return false;

    struct dirent* ent = nullptr;
    while ((ent = ::readdir(d)) != nullptr && out_count < kMaxEntries)
    {
        const char* name = ent->d_name;
        if (name == nullptr || name[0] == '\0' || std::strcmp(name, ".") == 0 ||
            std::strcmp(name, "..") == 0 || entry_hidden(dir, name))
            continue;

        char full[kPathCap];
        const int n = std::snprintf(full, sizeof(full), "%s/%s", dir, name);
        if (n <= 0 || static_cast<size_t>(n) >= sizeof(full))
            continue;

        bool     is_dir = false;
        uint32_t size   = 0;
        const unsigned char dtype = static_cast<unsigned char>(ent->d_type);

        if (dtype == DT_DIR)
        {
            is_dir = true;
        }
        else if (dtype == DT_REG)
        {
            struct stat st{};
            if (::stat(full, &st) != 0 || !S_ISREG(st.st_mode))
                continue;
            size = static_cast<uint32_t>(st.st_size);
        }
        else
        {
            struct stat st{};
            if (::stat(full, &st) != 0)
                continue;
            is_dir = S_ISDIR(st.st_mode);
            if (!is_dir && !S_ISREG(st.st_mode))
                continue;
            size = is_dir ? 0U : static_cast<uint32_t>(st.st_size);
        }

        Entry& e = out[out_count];
        std::strncpy(e.name, name, sizeof(e.name) - 1);
        e.name[sizeof(e.name) - 1] = '\0';
        e.is_dir                   = is_dir;
        e.size_bytes               = size;
        ++out_count;
    }

    if (ent != nullptr)
        truncated_out = true;

    ::closedir(d);

    if (out_count > 1)
        sort_entries(out, out_count);
    return true;
}

void VfsBrowser::adopt_scan(const Entry* entries, uint8_t count, bool truncated, bool has_parent_row)
{
    clear_entries();
    if (entries != nullptr && count > 0)
    {
        const uint8_t n = (count > kMaxEntries) ? kMaxEntries : count;
        std::memcpy(entries_, entries, sizeof(Entry) * n);
        entry_count_ = n;
    }
    truncated_      = truncated;
    has_parent_row_ = has_parent_row;
}

bool VfsBrowser::open_root(const char* mount_path, const char* vol_label)
{
    if (mount_path == nullptr || vol_label == nullptr || !path_allowed(mount_path))
        return false;

    auto& sto = ::app::common::storage::StorageMgr::get_instance();
    const Mount kind = ::app::common::storage::mount_kind_from_path_prefix(mount_path);
    if (kind == Mount::Invalid || !sto.is_mounted(kind))
        return false;

    mode_ = Mode::Browse;
    std::strncpy(cwd_, mount_path, sizeof(cwd_) - 1);
    cwd_[sizeof(cwd_) - 1] = '\0';
    std::strncpy(browse_title_, vol_label, sizeof(browse_title_) - 1);
    browse_title_[sizeof(browse_title_) - 1] = '\0';
    clear_entries();
    sync_parent_row();
    return true;
}

void VfsBrowser::reload()
{
    clear_entries();
    if (mode_ != Mode::Browse || cwd_[0] == '\0')
        return;

    auto& sto = ::app::common::storage::StorageMgr::get_instance();
    const Mount kind = ::app::common::storage::mount_kind_from_path_prefix(cwd_);
    if (kind != Mount::Invalid && !sto.is_mounted(kind))
    {
        EBOOK_LOGW(TAG, "volume unmounted: %s", cwd_);
        show_roots();
        return;
    }

    bool truncated = false;
    if (!scan_entries(cwd_, entries_, entry_count_, truncated))
    {
        EBOOK_LOGW(TAG, "scan failed: %s", cwd_);
        return;
    }
    truncated_ = truncated;
    sync_parent_row();
}

bool VfsBrowser::enter_dir(uint8_t entry_idx)
{
    if (mode_ != Mode::Browse || entry_idx >= entry_count_)
        return false;

    const Entry& e = entries_[entry_idx];
    if (!e.is_dir)
        return false;

    char next[kPathCap];
    const int n = std::snprintf(next, sizeof(next), "%s/%s", cwd_, e.name);
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(next) || !path_allowed(next))
        return false;

    std::strncpy(cwd_, next, sizeof(cwd_) - 1);
    cwd_[sizeof(cwd_) - 1] = '\0';
    std::strncpy(browse_title_, e.name, sizeof(browse_title_) - 1);
    browse_title_[sizeof(browse_title_) - 1] = '\0';
    clear_entries();
    sync_parent_row();
    return true;
}

bool VfsBrowser::entry_path(uint8_t entry_idx, char* out, size_t cap) const
{
    if (out == nullptr || cap == 0 || entry_idx >= entry_count_)
        return false;

    const Entry& e = entries_[entry_idx];
    if (e.name[0] == '\0' || e.is_dir)
        return false;

    const int n = std::snprintf(out, cap, "%s/%s", cwd_, e.name);
    return n > 0 && static_cast<size_t>(n) < cap;
}

bool VfsBrowser::delete_file(uint8_t entry_idx)
{
    char path[kPathCap];
    if (!entry_path(entry_idx, path, sizeof(path)))
        return false;

    if (::app::common::storage::path_is_system(path))
        return false;

    return ::remove(path) == 0;
}

bool VfsBrowser::go_up()
{
    if (mode_ != Mode::Browse)
        return false;

    if (is_storage_root(cwd_))
    {
        show_roots();
        return true;
    }

    char* slash = std::strrchr(cwd_, '/');
    if (slash == nullptr || slash == cwd_)
    {
        show_roots();
        return true;
    }
    *slash = '\0';
    if (!path_allowed(cwd_))
    {
        show_roots();
        return true;
    }

    sync_title_from_cwd();
    clear_entries();
    sync_parent_row();
    return true;
}

} // namespace app::ebook::apps::files
