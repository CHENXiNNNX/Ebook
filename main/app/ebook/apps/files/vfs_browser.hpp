#pragma once

#include <cstdint>

#include "storage/storage.hpp"

namespace app::ebook::apps::files {

/** 受控路径目录枚举（/int、/sd、/assets） */
class VfsBrowser
{
  public:
    static constexpr uint8_t kMaxEntries = 48;
    static constexpr uint8_t kPathCap    = 96;
    static constexpr uint8_t kNameCap    = 40;

    enum class Mode : uint8_t
    {
        Roots = 0,
        Browse,
    };

    struct Entry
    {
        char     name[kNameCap]{};
        bool     is_dir{false};
        uint32_t size_bytes{0};
    };

    struct RootVol
    {
        const char* path;
        const char* label;
        ::app::common::storage::MountKind kind;
    };

    static bool path_allowed(const char* path);
    static bool is_storage_root(const char* path);
    static uint8_t visible_root_count();
    static const RootVol* root_at_visible(uint8_t vis_idx);

    Mode mode() const { return mode_; }

    const char* cwd() const { return cwd_; }
    const char* browse_title() const { return browse_title_; }

    bool has_parent_row() const { return has_parent_row_; }
    uint8_t dir_entry_count() const { return entry_count_; }
    bool truncated() const { return truncated_; }

    uint8_t row_count() const;

    const Entry& entry_at(uint8_t idx) const;

    void show_roots();
    bool open_root(const char* mount_path, const char* vol_label);
    bool enter_dir(uint8_t entry_idx);
    bool go_up();
    void reload();

    static bool scan_entries(const char* dir, Entry* out, uint8_t& out_count, bool& truncated_out);
    void adopt_scan(const Entry* entries, uint8_t count, bool truncated, bool has_parent_row);

    bool entry_path(uint8_t entry_idx, char* out, size_t cap) const;
    bool delete_file(uint8_t entry_idx);

  private:
    void clear_entries();
    void sync_parent_row();
    void sync_title_from_cwd();
    static bool entry_hidden(const char* cwd, const char* name);

    Mode    mode_{Mode::Roots};
    char    cwd_[kPathCap]{};
    char    browse_title_[32]{};
    Entry   entries_[kMaxEntries]{};
    uint8_t entry_count_{0};
    bool    has_parent_row_{false};
    bool    truncated_{false};
};

} // namespace app::ebook::apps::files
