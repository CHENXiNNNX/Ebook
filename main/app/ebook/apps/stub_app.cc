#include "apps/stub_app.hpp"

#include "ui/strings.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"

namespace app::ebook::apps {

void StubApp::paint(gfx::Canvas& c)
{
    using ui::Theme;

    ui::widgets::toolbar(c, title_);

    if (icon_cp_ != 0)
    {
        const int16_t x = static_cast<int16_t>((Theme::kScreenW - Theme::kFontIconLg) / 2);
        const int16_t y = static_cast<int16_t>(Theme::kListStartY + 40);
        c.glyph(x, y, icon_cp_, Theme::kFontIconLg, gfx::FontFace::Icon);
    }

    const core::Rect text_box{
        0, static_cast<int16_t>(Theme::kListStartY + 40 + Theme::kFontIconLg + 12),
        Theme::kScreenW, static_cast<uint16_t>(Theme::kFontBody + 4)};
    gfx::Canvas::TextStyle s{};
    s.size_px = Theme::kFontBody;
    s.h       = gfx::HAlign::Center;
    s.v       = gfx::VAlign::Top;
    c.text_in(text_box, ui::strings::kWip, s);
}

} // namespace app::ebook::apps
