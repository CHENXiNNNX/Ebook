#pragma once

#include <cstdint>

#include "apps/reader/reader_layout.hpp"
#include "gfx/canvas.hpp"

namespace app::ebook::apps::reader {

enum class MenuHit : uint8_t
{
    None,
    FontPrev,
    FontNext,
    RefreshPrev,
    RefreshNext,
    JumpPrev10,
    PagePrev,
    PageNext,
    JumpNext10,
    Shelf,
    JumpPage,
    Lock,
};

struct MenuPaintInfo
{
    const char* font_label;
    const char* refresh_label;
    uint8_t     read_percent;
};

void paint_menu_panel(gfx::Canvas& c, const ReaderLayout& layout, const MenuPaintInfo& info);
MenuHit hit_menu(int16_t x, int16_t y, const ReaderLayout& layout);

} // namespace app::ebook::apps::reader
