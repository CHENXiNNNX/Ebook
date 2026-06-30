#include "overlays/keyboard.hpp"

#include <cstring>

#include "input/input_router.hpp"
#include "router/refresh_intent.hpp"
#include "router/router.hpp"
#include "ui/strings.hpp"
#include "ui/theme.hpp"

namespace app::ebook::overlays {

namespace {

using ui::Theme;

constexpr uint8_t kLetterRows = 6;
constexpr uint8_t kNumberRows = 4;
constexpr uint8_t kSymbolRows = 4;

// 字母页最后一行：z / Shift / 双格 Space / Backspace
constexpr char kLetters[kLetterRows][Theme::kKbCols + 1] = {
    "abcde",
    "fghij",
    "klmno",
    "pqrst",
    "uvwxy",
    "z    "
};

// 数字/符号页最后一列：@ _ / + 或 / : ; + Space + Backspace
constexpr char kNumbers[kNumberRows][Theme::kKbCols + 1] = {
    "12345",
    "67890",
    ".,!?-",
    "@_/  "
};

constexpr char kSymbols[kSymbolRows][Theme::kKbCols + 1] = {
    "@.-_!",
    "?#$%&",
    "*()+=",
    "/:;  "
};

constexpr uint16_t panel_y() { return Theme::kStatusBarH; }
constexpr uint16_t panel_h() { return Theme::kScreenH - Theme::kStatusBarH; }
constexpr uint16_t keys_y()  { return panel_y() + Theme::kKbInputH + Theme::kKbToolbarH; }
constexpr uint16_t keys_h()  { return Theme::kScreenH - keys_y(); }
constexpr uint16_t col_w()   { return Theme::kScreenW / Theme::kKbCols; }

} // namespace

Keyboard& Keyboard::instance()
{
    static Keyboard s;
    return s;
}

core::Rect Keyboard::bounds() const
{
    return core::Rect{0, static_cast<int16_t>(panel_y()), Theme::kScreenW, panel_h()};
}

core::Rect Keyboard::input_rect() const
{
    return core::Rect{0, static_cast<int16_t>(panel_y()), Theme::kScreenW, Theme::kKbInputH};
}

core::Rect Keyboard::toolbar_rect() const
{
    return core::Rect{0, static_cast<int16_t>(panel_y() + Theme::kKbInputH),
                      Theme::kScreenW, Theme::kKbToolbarH};
}

core::Rect Keyboard::keys_rect() const
{
    return core::Rect{0, static_cast<int16_t>(keys_y()), Theme::kScreenW, keys_h()};
}

void Keyboard::buf_clear() { buf_[0] = '\0'; buf_len_ = 0; }

void Keyboard::buf_append(char c)
{
    if (buf_len_ >= max_len_) return;
    buf_[buf_len_++] = c;
    buf_[buf_len_]   = '\0';
}

void Keyboard::buf_backspace()
{
    if (buf_len_ == 0) return;
    --buf_len_;
    buf_[buf_len_] = '\0';
}

void Keyboard::buf_set(const char* s)
{
    buf_clear();
    if (s == nullptr) return;
    while (*s != '\0' && buf_len_ < max_len_)
        buf_[buf_len_++] = *s++;
    buf_[buf_len_] = '\0';
}

void Keyboard::open(const KeyboardConfig& cfg, KeyboardCallback cb, void* user)
{
    layer_   = cfg.default_layer;
    upper_   = false;
    cb_      = cb;
    user_    = user;
    max_len_ = (cfg.max_len > kBufCap) ? kBufCap : cfg.max_len;

    if (cfg.title != nullptr)
    {
        std::strncpy(title_, cfg.title, sizeof(title_) - 1);
        title_[sizeof(title_) - 1] = '\0';
    }
    else
    {
        title_[0] = '\0';
    }
    buf_set(cfg.initial_text);
    ++open_gen_;
    open_ = true;

    ::app::ebook::input::InputRouter::get_instance().set_profile(
        ::app::ebook::input::Profile::Keyboard);

    (void)router::Router::instance().repaint(router::intent_partial_full());
}

void Keyboard::close()
{
    open_ = false;
    cb_   = nullptr;
    user_ = nullptr;
    title_[0] = '\0';
    buf_clear();

    ::app::ebook::input::InputRouter::get_instance().set_profile(
        ::app::ebook::input::Profile::Normal);

    (void)router::Router::instance().repaint(router::intent_partial_full());
}

uint8_t Keyboard::rows_of_current_layer() const
{
    switch (layer_)
    {
        case KeyboardLayer::Letters: return kLetterRows;
        case KeyboardLayer::Numbers: return kNumberRows;
        case KeyboardLayer::Symbols: return kSymbolRows;
    }
    return 0;
}

bool Keyboard::is_shift_cell(uint8_t row, uint8_t col) const
{
    return layer_ == KeyboardLayer::Letters && row == kLetterRows - 1 && col == 1;
}

bool Keyboard::is_space_cell(uint8_t row, uint8_t col) const
{
    const uint8_t last = static_cast<uint8_t>(rows_of_current_layer() - 1);
    if (row != last) return false;
    if (layer_ == KeyboardLayer::Letters)  return col == 2 || col == 3;
    return col == 3;
}

bool Keyboard::is_backspace_cell(uint8_t row, uint8_t col) const
{
    return row == static_cast<uint8_t>(rows_of_current_layer() - 1) && col == 4;
}

char Keyboard::char_at(uint8_t row, uint8_t col) const
{
    if (row >= rows_of_current_layer() || col >= Theme::kKbCols) return 0;
    if (is_shift_cell(row, col) || is_space_cell(row, col) || is_backspace_cell(row, col))
        return 0;

    char ch = 0;
    switch (layer_)
    {
        case KeyboardLayer::Letters: ch = kLetters[row][col]; break;
        case KeyboardLayer::Numbers: ch = kNumbers[row][col]; break;
        case KeyboardLayer::Symbols: ch = kSymbols[row][col]; break;
    }
    if (ch == ' ' || ch == 0) return 0;
    if (layer_ == KeyboardLayer::Letters && upper_)
        ch = static_cast<char>(ch - 'a' + 'A');
    return ch;
}

void Keyboard::draw_key(gfx::Canvas& c, const core::Rect& r, const char* label, bool inverted)
{
    c.rect(r);
    if (label == nullptr || label[0] == '\0') return;

    gfx::Canvas::TextStyle s{};
    s.size_px = Theme::kFontBody;
    s.h       = gfx::HAlign::Center;
    s.v       = gfx::VAlign::Middle;
    s.padding = 2;
    c.text_in(r, label, s);

    if (inverted) c.invert(r);
}

void Keyboard::paint_input(gfx::Canvas& c)
{
    const core::Rect r = input_rect();
    c.fill(r, gfx::Ink::White);
    c.rect(core::Rect{
        static_cast<int16_t>(r.x + Theme::kPadLg),
        static_cast<int16_t>(r.y + 4),
        static_cast<uint16_t>(r.w - Theme::kPadLg * 2),
        static_cast<uint16_t>(r.h - 8)});

    const char* show = (buf_len_ == 0) ? title_ : buf_;
    if (show != nullptr && show[0] != '\0')
    {
        const int16_t tx = static_cast<int16_t>(r.x + Theme::kPadLg + 4);
        const int16_t ty = static_cast<int16_t>(r.y + (r.h - Theme::kFontBody) / 2);
        const uint8_t fs = (buf_len_ == 0) ? Theme::kFontSmall : Theme::kFontBody;
        c.text(tx, ty, show, fs);
    }
}

void Keyboard::paint_toolbar(gfx::Canvas& c)
{
    const core::Rect bar = toolbar_rect();
    c.fill(bar, gfx::Ink::White);
    c.hline(0, bar.y, Theme::kScreenW);

    const uint16_t cw = col_w();

    struct Btn { core::Rect r; const char* lbl; bool active; };
    const Btn btns[] = {
        {{0, bar.y, cw, bar.h},
         ui::strings::kKbExit, false},
        {{static_cast<int16_t>(cw), bar.y, cw, bar.h},
         ui::strings::kKb123, layer_ == KeyboardLayer::Numbers},
        {{static_cast<int16_t>(cw * 2), bar.y, cw, bar.h},
         ui::strings::kKbABC, layer_ == KeyboardLayer::Letters},
        {{static_cast<int16_t>(cw * 3), bar.y, cw, bar.h},
         ui::strings::kKbSymbol, layer_ == KeyboardLayer::Symbols},
        {{static_cast<int16_t>(cw * 4), bar.y, cw, bar.h},
         ui::strings::kKbDone, false},
    };
    for (const Btn& b : btns) draw_key(c, b.r, b.lbl, b.active);
}

void Keyboard::paint_keys(gfx::Canvas& c)
{
    const core::Rect area = keys_rect();
    c.fill(area, gfx::Ink::White);

    const uint8_t  rows  = rows_of_current_layer();
    if (rows == 0) return;
    const uint16_t row_h = static_cast<uint16_t>(area.h / rows);

    for (uint8_t row = 0; row < rows; ++row)
    {
        const int16_t y = static_cast<int16_t>(area.y + row * row_h);
        for (uint8_t col = 0; col < Theme::kKbCols; ++col)
        {
            core::Rect cell{static_cast<int16_t>(col * col_w()), y, col_w(), row_h};

            if (is_shift_cell(row, col))
            {
                draw_key(c, cell, upper_ ? "AA" : "Aa", upper_);
                continue;
            }
            if (is_space_cell(row, col))
            {
                // 字母页双格 space：col=2 占 2 列宽，col=3 跳过
                if (layer_ == KeyboardLayer::Letters)
                {
                    if (col == 2)
                    {
                        cell.w = static_cast<uint16_t>(cell.w * 2);
                        draw_key(c, cell, ui::strings::kKbSpace, false);
                    }
                }
                else
                {
                    draw_key(c, cell, ui::strings::kKbSpace, false);
                }
                continue;
            }
            if (is_backspace_cell(row, col))
            {
                draw_key(c, cell, "\u2190", false);
                continue;
            }

            const char ch = char_at(row, col);
            if (ch == 0) continue;
            const char label[2] = {ch, '\0'};
            draw_key(c, cell, label, false);
        }
    }
}

void Keyboard::paint(gfx::Canvas& c)
{
    if (!open_) return;
    const core::Rect b = bounds();
    c.fill(b, gfx::Ink::White);
    paint_input(c);
    paint_toolbar(c);
    paint_keys(c);
    c.rect(b);
}

ui::InputResult Keyboard::on_input(const ::app::ebook::input::Event& ev)
{
    using ::app::ebook::input::EventType;
    if (!open_) return {};
    if (ev.type != EventType::Tap) return {true};

    const int16_t x = static_cast<int16_t>(ev.x);
    const int16_t y = static_cast<int16_t>(ev.y);

    const core::Rect tb = toolbar_rect();
    if (tb.contains(x, y))
    {
        const uint8_t col = static_cast<uint8_t>(x / col_w());
        switch (col)
        {
            case 0:
                close();
                return {true};
            case 1:
                layer_ = KeyboardLayer::Numbers;
                upper_ = false;
                (void)router::Router::instance().repaint(router::intent_partial_full());
                return {true};
            case 2:
                layer_ = KeyboardLayer::Letters;
                (void)router::Router::instance().repaint(router::intent_partial_full());
                return {true};
            case 3:
                layer_ = KeyboardLayer::Symbols;
                upper_ = false;
                (void)router::Router::instance().repaint(router::intent_partial_full());
                return {true};
            default:
            {
                const uint32_t gen = open_gen_;
                if (cb_ != nullptr)
                    cb_(buf_, user_);
                // 回调内可再次 open()；未重开时才 close
                if (open_gen_ == gen)
                    close();
                return {true};
            }
        }
    }

    const core::Rect ka = keys_rect();
    if (!ka.contains(x, y)) return {true};

    const uint8_t  rows  = rows_of_current_layer();
    if (rows == 0) return {true};
    const uint16_t row_h = static_cast<uint16_t>(ka.h / rows);
    const uint8_t  row   = static_cast<uint8_t>((y - ka.y) / row_h);
    const uint8_t  col   = static_cast<uint8_t>(x / col_w());
    if (row >= rows || col >= Theme::kKbCols) return {true};

    if (is_shift_cell(row, col))
    {
        upper_ = !upper_;
        (void)router::Router::instance().repaint(router::intent_partial_full());
        return {true};
    }
    if (is_space_cell(row, col))
    {
        buf_append(' ');
        (void)router::Router::instance().repaint(router::intent_partial_full());
        return {true};
    }
    if (is_backspace_cell(row, col))
    {
        buf_backspace();
        (void)router::Router::instance().repaint(router::intent_partial_full());
        return {true};
    }

    const char ch = char_at(row, col);
    if (ch != 0)
    {
        buf_append(ch);
        (void)router::Router::instance().repaint(router::intent_partial_full());
    }
    return {true};
}

} // namespace app::ebook::overlays
