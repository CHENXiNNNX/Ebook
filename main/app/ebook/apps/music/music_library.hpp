#pragma once

#include <cstdint>

namespace app::ebook::apps::music {

struct MusicTrack
{
    char     path[128];
    char     title[48];
    uint32_t size_bytes;
    uint16_t duration_sec;
};

class MusicLibrary
{
  public:
    static constexpr uint8_t kMaxTracks = 64;

    /** 与 is_audio() / scan_dir() 一致，供空状态提示 */
    static constexpr const char* kScanFmtHint  = "MP3 / WAV";
    static constexpr const char* kScanPathInt  = "/int/Ebook/music";
    static constexpr const char* kScanPathSd   = "/sd/Ebook/music";

    static MusicLibrary& get_instance();

    bool scan();

    bool add_track_from_path(const char* full_path);
    int8_t find_index(const char* full_path) const;

    uint8_t            count()         const { return count_; }
    const MusicTrack&  at(uint8_t idx) const;

  private:
    MusicLibrary() = default;

    bool scan_dir(const char* dir, MusicTrack* out, uint8_t& out_count);
    bool add_track(const char* full_path, const char* name, uint32_t size,
                   MusicTrack* out, uint8_t& out_count);
    bool parse_wav_duration(const char* path, uint16_t& sec_out) const;

    MusicTrack tracks_[kMaxTracks]{};
    MusicTrack scan_buf_[kMaxTracks]{};
    uint8_t    count_{0};
};

} // namespace app::ebook::apps::music
