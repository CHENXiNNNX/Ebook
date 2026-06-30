#include "display/display_port.hpp"

#include "core/log.hpp"

static const char* const TAG = "DisplayPort";

namespace app::ebook::display {

namespace {

using Present = ::app::bsp::driver::gdey027t91::Present;
using Window  = ::app::bsp::driver::gdey027t91::Window;

Window make_window(const core::Rect& r)
{
    Window w{};
    w.x = static_cast<uint16_t>(r.x);
    w.y = static_cast<uint16_t>(r.y);
    w.w = r.w;
    w.h = r.h;
    return w;
}

} // namespace

DisplayPort& DisplayPort::instance()
{
    static DisplayPort s;
    return s;
}

bool DisplayPort::init()
{
    if (ready_)
        return true;

    if (!panel_.init())
    {
        EBOOK_LOGE(TAG, "panel init failed");
        return false;
    }
    if (!backlight_.init())
        EBOOK_LOGW(TAG, "backlight init failed");

    panel_.fill(0xFF);
    panel_.invalidate_session();
    ready_ = true;
    EBOOK_LOGI(TAG, "ready");
    return true;
}

void DisplayPort::deinit()
{
    if (!ready_)
        return;
    panel_.deinit();
    backlight_.deinit();
    ready_ = false;
}

uint8_t* DisplayPort::framebuffer()
{
    return panel_.fb();
}

SessionState DisplayPort::session() const
{
    if (!ready_)
        return SessionState::Cold;
    return panel_.session_ready() ? SessionState::PartialReady : SessionState::Cold;
}

void DisplayPort::invalidate_session()
{
    if (ready_)
        panel_.invalidate_session();
}

core::Status DisplayPort::bootstrap()
{
    if (!ready_)
        return core::Status::NotInit;
    return panel_.present(Present::Base) ? core::Status::Ok : core::Status::IoError;
}

core::Status DisplayPort::partial(const core::Rect& rect)
{
    if (!ready_ || rect.empty())
        return core::Status::InvalidArg;
    return panel_.present(Present::Partial, make_window(rect))
               ? core::Status::Ok
               : core::Status::IoError;
}

core::Status DisplayPort::fast()
{
    if (!ready_)
        return core::Status::NotInit;
    return panel_.present(Present::Fast) ? core::Status::Ok : core::Status::IoError;
}

core::Status DisplayPort::full()
{
    if (!ready_)
        return core::Status::NotInit;
    return panel_.present(Present::Full) ? core::Status::Ok : core::Status::IoError;
}

void DisplayPort::set_brightness(uint8_t percent)
{
    if (percent > 100)
        percent = 100;
    backlight_.set_brightness(percent);
}

uint8_t DisplayPort::brightness() const
{
    return backlight_.brightness();
}

} // namespace app::ebook::display
