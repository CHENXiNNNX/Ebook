#pragma once

#include <cstddef>
#include <cstdint>

#include "gfx/font.hpp"

namespace app::ebook::gfx {

/**
 * @brief 预烘焙位图字库（simhei_12/14/16.bin，magic "EBF1" v1，整文件载入 PSRAM）
 *
 * 布局：FileHeader(28B) + cp→gid LUT + DirEntry[16B] + 1-bit bitmap pool。
 */
class BitmapFontFace
{
  public:
    BitmapFontFace() = default;
    ~BitmapFontFace() { deinit(); }
    BitmapFontFace(const BitmapFontFace&) = delete;
    BitmapFontFace& operator=(const BitmapFontFace&) = delete;

    bool init(const char* path);
    void deinit();

    bool    ready()       const { return data_ != nullptr; }
    uint8_t size_px()     const { return size_px_; }
    uint16_t ascent()     const { return ascent_; }
    uint16_t line_height() const { return line_height_; }

    bool     lookup(uint32_t cp, GlyphMetrics& m, const uint8_t*& bitmap) const;
    uint16_t advance(uint32_t cp) const;

  private:
    struct DirEntry
    {
        uint16_t advance;
        int16_t  bearing_x;
        int16_t  bearing_y;
        uint16_t width;
        uint16_t height;
        uint16_t pitch;
        uint32_t bitmap_ofs;
    };
    static_assert(sizeof(DirEntry) == 16, "DirEntry layout (matches simhei_*.bin format)");

    uint8_t*        data_{nullptr};
    size_t          data_size_{0};
    const uint16_t* lut_{nullptr};
    const DirEntry* dir_{nullptr};
    const uint8_t*  pool_{nullptr};
    uint16_t        glyph_count_{0};
    uint8_t         size_px_{0};
    uint8_t         ascent_{0};
    uint8_t         line_height_{0};
};

/** @brief 12 / 14 / 16 px 预烘焙字库集合（/assets/fonts） */
class BitmapFontSet
{
  public:
    static constexpr uint8_t kSizes[]     = {12, 14, 16};
    static constexpr size_t  kSizeCount    = sizeof(kSizes) / sizeof(kSizes[0]);

    static BitmapFontSet& get_instance();

    bool init();
    void deinit();

    bool any_ready() const;

    const BitmapFontFace* get(uint8_t size_px) const;
    const BitmapFontFace* nearest(uint8_t size_px) const;

  private:
    BitmapFontSet() = default;
    ~BitmapFontSet() { deinit(); }

    BitmapFontFace faces_[kSizeCount]{};
    uint8_t        loaded_{0};
};

} // namespace app::ebook::gfx
