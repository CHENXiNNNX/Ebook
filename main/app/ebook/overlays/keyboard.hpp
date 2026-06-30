#pragma once

#include <cstdint>

#include "ui/layer.hpp"

namespace app::ebook::overlays {

enum class KeyboardLayer : uint8_t
{
    Letters = 0,
    Numbers,
    Symbols,
};

using KeyboardCallback = void (*)(const char* text, void* user_data);

struct KeyboardConfig
{
    KeyboardLayer default_layer{KeyboardLayer::Letters};
    uint8_t       max_len{32};
    const char*   initial_text{nullptr};
    const char*   title{nullptr};
};

/** @brief 软键盘（Letters/Numbers/Symbols；open 时切换 Input Profile::Keyboard） */
class Keyboard : public ui::Layer
{
  public:
    static Keyboard& instance();

    bool        is_open() const { return open_; }
    const char* buffer()  const { return buf_; }

    void open(const KeyboardConfig& cfg, KeyboardCallback cb, void* user = nullptr);
    void close();

    bool visible() const override { return open_; }
    bool modal()   const override { return open_; }
    bool wants_status_bar() const override { return false; }
    core::Rect bounds() const override;

    ui::InputResult on_input(const ::app::ebook::input::Event& ev) override;
    void paint(gfx::Canvas& canvas) override;

  private:
    Keyboard() = default;

    static constexpr uint8_t kBufCap = 64;

    void buf_clear();
    void buf_append(char c);
    void buf_backspace();
    void buf_set(const char* s);

    uint8_t rows_of_current_layer() const;
    char    char_at(uint8_t row, uint8_t col) const;
    bool    is_shift_cell(uint8_t row, uint8_t col) const;
    bool    is_space_cell(uint8_t row, uint8_t col) const;
    bool    is_backspace_cell(uint8_t row, uint8_t col) const;

    core::Rect input_rect() const;
    core::Rect toolbar_rect() const;
    core::Rect keys_rect() const;

    void paint_input(gfx::Canvas& c);
    void paint_toolbar(gfx::Canvas& c);
    void paint_keys(gfx::Canvas& c);
    void draw_key(gfx::Canvas& c, const core::Rect& r, const char* label, bool inverted);

    char    buf_[kBufCap + 1]{};
    uint8_t buf_len_{0};
    uint8_t max_len_{kBufCap};

    KeyboardLayer    layer_{KeyboardLayer::Letters};
    bool             upper_{false};
    bool             open_{false};
    KeyboardCallback cb_{nullptr};
    void*            user_{nullptr};
    char             title_[48]{};
    uint32_t         open_gen_{0};
};

} // namespace app::ebook::overlays
