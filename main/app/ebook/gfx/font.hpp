#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>

#include "core/geometry.hpp"

struct FT_LibraryRec_;
struct FT_FaceRec_;
typedef struct FT_LibraryRec_* FT_Library;
typedef struct FT_FaceRec_* FT_Face;

namespace app::ebook::gfx {

enum class FontFace : uint8_t
{
    Text = 0,
    Icon = 1,
};

enum class Ink : uint8_t
{
    Black = 0,
    White = 1,
};

struct GlyphMetrics
{
    int16_t  bearing_x{0};
    int16_t  bearing_y{0};
    uint16_t width{0};
    uint16_t height{0};
    uint16_t advance{0};
};

/**
 * @brief 1-bit 字体：BitmapFont 12/14/16 + FreeType fallback + LRU 缓存
 */
class Font
{
  public:
    static constexpr size_t kCacheSlots = 256;

    static Font& get_instance();

    bool init();
    void deinit();
    bool ready() const { return ready_; }

    uint16_t draw_glyph(uint8_t* fb, int16_t x, int16_t y_top, uint32_t cp,
                        uint8_t size_px, FontFace face, Ink ink, const core::Rect& clip);

    uint16_t draw_text(uint8_t* fb, int16_t x, int16_t y_top, const char* utf8,
                       uint8_t size_px, FontFace face, Ink ink, const core::Rect& clip);

    uint16_t advance(uint32_t cp, uint8_t size_px, FontFace face);
    uint16_t measure(const char* utf8, uint8_t size_px, FontFace face = FontFace::Text);
    uint16_t line_height(uint8_t size_px, FontFace face = FontFace::Text);
    uint16_t ascent(uint8_t size_px, FontFace face = FontFace::Text);

    struct Stats
    {
        uint32_t hits{0};
        uint32_t misses{0};
        uint32_t evictions{0};
        uint32_t bitmap_bytes{0};
    };
    Stats stats() const { return stats_; }
    void  reset_stats()  { stats_ = {}; }

  private:
    Font() = default;
    ~Font() = default;
    Font(const Font&) = delete;
    Font& operator=(const Font&) = delete;

    struct Slot
    {
        uint32_t key{0};
        uint32_t lru{0};
        GlyphMetrics m{};
        uint8_t* bitmap{nullptr};
        uint16_t bitmap_bytes{0};
    };

    static uint32_t make_key(uint32_t cp, uint8_t size_px, FontFace face);
    static size_t   hash_slot(uint32_t key);

    Slot* cache_find_locked(uint32_t key);
    Slot* cache_replace_locked(uint32_t key);
    Slot* render_into_locked(uint32_t cp, uint8_t size_px, FontFace face);

    FT_Face active_face_locked(FontFace face);
    bool    select_size_locked(FontFace face, uint8_t size_px);

    void blit_locked(uint8_t* fb, int16_t pen_x, int16_t baseline_y,
                     const GlyphMetrics& m, const uint8_t* bitmap,
                     Ink ink, const core::Rect& clip);

    void blit_bitmap_glyph_locked(uint8_t* fb, int16_t pen_x, int16_t baseline_y,
                                  const GlyphMetrics& m, const uint8_t* bitmap,
                                  uint8_t size_px, Ink ink, const core::Rect& clip);

    FT_Library lib_{nullptr};
    FT_Face    face_text_{nullptr};
    FT_Face    face_icon_{nullptr};
    uint8_t    size_text_{0};
    uint8_t    size_icon_{0};
    bool       ready_{false};
    bool       bitmap_only_text_{false};

    Slot        cache_[kCacheSlots]{};
    uint32_t    access_seq_{0};
    Stats       stats_{};

    mutable std::mutex mtx_;
};

} // namespace app::ebook::gfx
