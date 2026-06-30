#include "apps/music/music_library.hpp"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

#include "storage/storage.hpp"
#include "text/path_encoding.hpp"

namespace app::ebook::apps::music {

namespace {

constexpr const char* kDirInt = MusicLibrary::kScanPathInt;
constexpr const char* kDirSd  = MusicLibrary::kScanPathSd;

bool ext_lower(const char* name, char* out, size_t cap)
{
    if (name == nullptr || out == nullptr || cap == 0) return false;
    const char* dot = std::strrchr(name, '.');
    if (dot == nullptr) { out[0] = '\0'; return false; }
    ++dot;
    size_t i = 0;
    for (; dot[i] != '\0' && i < cap - 1; ++i)
        out[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(dot[i])));
    out[i] = '\0';
    return true;
}

bool is_audio(const char* name)
{
    char ext[8]{};
    if (!ext_lower(name, ext, sizeof(ext))) return false;
    return (std::strcmp(ext, "mp3") == 0 || std::strcmp(ext, "wav") == 0);
}

void strip_ext(const char* name, char* out, size_t cap)
{
    if (name == nullptr || out == nullptr || cap == 0) return;
    std::strncpy(out, name, cap - 1);
    out[cap - 1] = '\0';
    char* dot = std::strrchr(out, '.');
    if (dot != nullptr) *dot = '\0';
}

uint32_t read_le32(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}
uint16_t read_le16(const uint8_t* p)
{
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

} // namespace

MusicLibrary& MusicLibrary::get_instance()
{
    static MusicLibrary s;
    return s;
}

const MusicTrack& MusicLibrary::at(uint8_t idx) const
{
    static const MusicTrack kEmpty{};
    return (idx < count_) ? tracks_[idx] : kEmpty;
}

bool MusicLibrary::parse_wav_duration(const char* path, uint16_t& sec_out) const
{
    sec_out = 0;
    FILE* f = std::fopen(path, "rb");
    if (f == nullptr) return false;

    uint8_t hdr[128];
    const size_t n = std::fread(hdr, 1, sizeof(hdr), f);
    std::fclose(f);
    if (n < 44) return false;

    if (std::memcmp(hdr, "RIFF", 4) != 0 ||
        std::memcmp(hdr + 8, "WAVE", 4) != 0)
        return false;

    uint32_t rate = 0, data_sz = 0;
    uint16_t channels = 0, bits = 0;

    // 扫描 fmt + data chunks
    size_t off = 12;
    while (off + 8 <= n)
    {
        const char* id = reinterpret_cast<const char*>(hdr + off);
        const uint32_t sz = read_le32(hdr + off + 4);
        off += 8;
        if (off + sz > n) break;

        if (std::memcmp(id, "fmt ", 4) == 0 && sz >= 16)
        {
            channels = read_le16(hdr + off + 2);
            rate     = read_le32(hdr + off + 4);
            bits     = read_le16(hdr + off + 14);
        }
        else if (std::memcmp(id, "data", 4) == 0)
        {
            data_sz = sz;
        }
        off += sz;
    }

    if (rate == 0 || channels == 0 || bits == 0 || data_sz == 0) return false;
    const uint32_t bps = rate * channels * (static_cast<uint32_t>(bits) / 8U);
    if (bps == 0) return false;
    const uint32_t sec = data_sz / bps;
    sec_out = (sec > 0xFFFFU) ? 0xFFFFU : static_cast<uint16_t>(sec);
    return sec_out > 0;
}

bool MusicLibrary::add_track(const char* full_path, const char* name, uint32_t size,
                             MusicTrack* out, uint8_t& out_count)
{
    if (out_count >= kMaxTracks || full_path == nullptr || name == nullptr)
        return false;

    MusicTrack& t = out[out_count];
    std::strncpy(t.path, full_path, sizeof(t.path) - 1);
    t.path[sizeof(t.path) - 1] = '\0';

    char norm[48];
    (void)text::normalize_path_segment(name, norm, sizeof(norm));
    strip_ext(norm, t.title, sizeof(t.title));

    t.size_bytes   = size;
    t.duration_sec = 0;

    char ext[8]{};
    if (ext_lower(name, ext, sizeof(ext)) && std::strcmp(ext, "wav") == 0)
        (void)parse_wav_duration(full_path, t.duration_sec);

    ++out_count;
    return true;
}

int8_t MusicLibrary::find_index(const char* full_path) const
{
    if (full_path == nullptr)
        return -1;
    for (uint8_t i = 0; i < count_; ++i)
    {
        if (std::strcmp(tracks_[i].path, full_path) == 0)
            return static_cast<int8_t>(i);
    }
    return -1;
}

bool MusicLibrary::add_track_from_path(const char* full_path)
{
    if (full_path == nullptr)
        return false;
    if (find_index(full_path) >= 0)
        return true;
    if (count_ >= kMaxTracks)
        return false;

    struct stat st{};
    if (stat(full_path, &st) != 0 || !S_ISREG(st.st_mode))
        return false;

    const char* name = std::strrchr(full_path, '/');
    name             = (name != nullptr) ? (name + 1) : full_path;
    if (!is_audio(name))
        return false;

    return add_track(full_path, name, static_cast<uint32_t>(st.st_size),
                     tracks_, count_);
}

bool MusicLibrary::scan_dir(const char* dir, MusicTrack* out, uint8_t& out_count)
{
    if (dir == nullptr || out == nullptr)
        return false;

    struct stat dst{};
    if (stat(dir, &dst) != 0 || !S_ISDIR(dst.st_mode))
        return false;

    DIR* d = opendir(dir);
    if (d == nullptr)
        return false;

    constexpr size_t kPathCap = 128;

    struct dirent* ent;
    while ((ent = readdir(d)) != nullptr && out_count < kMaxTracks)
    {
        const char* name = ent->d_name;
        if (name == nullptr || name[0] == '.' || !is_audio(name))
            continue;

        const size_t dir_len  = std::strlen(dir);
        const size_t name_len = std::strlen(name);
        if (dir_len + 1 + name_len >= kPathCap)
            continue;

        char full[kPathCap];
        const int n = std::snprintf(full, sizeof(full), "%s/%s", dir, name);
        if (n < 0 || static_cast<size_t>(n) >= kPathCap)
            continue;

        struct stat st{};
        if (stat(full, &st) != 0 || !S_ISREG(st.st_mode))
            continue;

        (void)add_track(full, name, static_cast<uint32_t>(st.st_size), out, out_count);
    }

    closedir(d);
    return true;
}

bool MusicLibrary::scan()
{
    uint8_t n = 0;
    std::memset(scan_buf_, 0, sizeof(scan_buf_));

    auto& sto = ::app::common::storage::StorageMgr::get_instance();
    if (sto.is_mounted(::app::common::storage::MountKind::Internal))
        (void)scan_dir(kDirInt, scan_buf_, n);
    if (sto.is_mounted(::app::common::storage::MountKind::Sd))
        (void)scan_dir(kDirSd, scan_buf_, n);

    std::memcpy(tracks_, scan_buf_, sizeof(MusicTrack) * n);
    count_ = n;
    return count_ > 0;
}

} // namespace app::ebook::apps::music
