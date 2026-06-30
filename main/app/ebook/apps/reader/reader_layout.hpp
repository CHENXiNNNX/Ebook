#pragma once

#include <cstdint>

#include "core/geometry.hpp"
#include "apps/reader/txt_index_cache.hpp"
#include "gfx/font.hpp"
#include "ui/theme.hpp"

namespace app::ebook::apps::reader {

/** 阅读器版面（竖屏 176×264，随字号变化） */
class ReaderLayout
{
  public:
    static constexpr uint8_t  kFontSizes[3] = {12, 14, 16};
    static constexpr uint16_t kTopBarH      = 18;
    static constexpr uint16_t kPadH         = 8;
    static constexpr uint16_t kContentPadY  = 4;

    /** 底栏菜单：浮在正文上方，贴屏幕底边 */
    static constexpr uint16_t kMenuRowH  = 20;
    static constexpr uint16_t kMenuRowGap = 1;
    static constexpr uint8_t  kMenuRows  = 4;
    static constexpr uint16_t kMenuPanelH =
        kMenuRowH * kMenuRows + kMenuRowGap * (kMenuRows - 1);

    explicit ReaderLayout(uint8_t font_px = 12) : font_px_(font_px) {}

    uint8_t font_px() const { return font_px_; }

    uint16_t line_height() const
    {
        return gfx::Font::get_instance().line_height(font_px_, gfx::FontFace::Text);
    }

    uint16_t text_width() const
    {
        return static_cast<uint16_t>(ui::Theme::kScreenW - kPadH * 2);
    }

    /** 正文区：顶栏以下至屏幕底，不受菜单显隐影响 */
    core::Rect content_rect() const
    {
        const uint16_t h =
            static_cast<uint16_t>(ui::Theme::kScreenH - kTopBarH);
        return core::Rect{static_cast<int16_t>(kPadH),
                          static_cast<int16_t>(kTopBarH),
                          text_width(),
                          h};
    }

    uint8_t lines_per_page() const
    {
        const core::Rect area = content_rect();
        const uint16_t lh     = line_height();
        if (lh == 0)
            return 1;
        const uint16_t usable =
            (area.h > kContentPadY * 2)
                ? static_cast<uint16_t>(area.h - kContentPadY * 2)
                : area.h;
        uint8_t n = static_cast<uint8_t>(usable / lh);
        return (n < 1) ? 1 : n;
    }

    core::Rect top_bar_rect() const
    {
        return core::Rect{0, 0, ui::Theme::kScreenW, kTopBarH};
    }

    int16_t menu_panel_y() const
    {
        return static_cast<int16_t>(ui::Theme::kScreenH - kMenuPanelH);
    }

    core::Rect menu_panel_rect() const
    {
        return core::Rect{0, menu_panel_y(), ui::Theme::kScreenW, kMenuPanelH};
    }

    TxtIndexLayout make_index_layout(uint8_t text_encoding,
                                     uint32_t file_size) const
    {
        const core::Rect area = content_rect();
        TxtIndexLayout l{};
        l.viewport_w     = area.w;
        l.viewport_h     = area.h;
        l.lines_per_page = lines_per_page();
        l.font_size      = font_px_;
        l.line_height    = line_height();
        l.screen_w       = ui::Theme::kScreenW;
        l.screen_h       = ui::Theme::kScreenH;
        l.file_size      = file_size;
        l.text_encoding  = text_encoding;
        return l;
    }

  private:
    uint8_t font_px_{12};
};

} // namespace app::ebook::apps::reader
