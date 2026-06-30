#pragma once

#include <cstdint>

#include "gfx/canvas.hpp"
#include "input/input_event.hpp"
#include "shell/page.hpp"

namespace app::ebook::overlays {

/** @brief 是/否确认框（省电等系统提示） */
class ConfirmPrompt
{
  public:
    using ChoiceHandler = void (*)(bool accepted, void* user);

    static ConfirmPrompt& instance();

    bool is_open() const { return open_; }

    void show(const char* message, ChoiceHandler handler, void* user);
    void close();

    void paint(gfx::Canvas& canvas);
    shell::InputResult on_input(const ::app::ebook::input::Event& ev);

  private:
    ConfirmPrompt() = default;

    static constexpr size_t kMsgMax = 80;

    char           message_[kMsgMax + 1]{};
    ChoiceHandler  handler_{nullptr};
    void*          user_{nullptr};
    bool           open_{false};
};

} // namespace app::ebook::overlays
