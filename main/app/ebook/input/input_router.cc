#include "input/input_router.hpp"

#include "core/log.hpp"
#include "ui/ui_bus.hpp"

static const char* const TAG = "InputRouter";

namespace app::ebook::input {

InputRouter& InputRouter::get_instance()
{
    static InputRouter s;
    return s;
}

bool InputRouter::init(::app::bsp::driver::ft6336u::Ft6336u* touch)
{
    if (touch == nullptr || !touch->is_init())
    {
        EBOOK_LOGE(TAG, "touch device not ready");
        return false;
    }
    dev_ = touch;
    cfg_ = make_config(Profile::Normal);

    if (!dev_->interrupt_enabled() && !dev_->enable_interrupt())
        EBOOK_LOGW(TAG, "INT not enabled");

    if (!gesture_.init(dev_, &cfg_))
    {
        EBOOK_LOGE(TAG, "gesture init failed");
        return false;
    }
    return true;
}

void InputRouter::deinit()
{
    stop();
    dev_ = nullptr;
}

bool InputRouter::start()
{
    if (running_) return true;

    ::app::sys::task::Cfg cfg;
    cfg.name       = "ebook_touch";
    cfg.stack_size = kStackBytes;
    cfg.priority   = ::app::sys::task::Priority::REALTIME;
    cfg.core_id    = tskNO_AFFINITY;

    running_ = true;
    task_ = std::make_unique<::app::sys::task::Task>(
        [](void* arg) { static_cast<InputRouter*>(arg)->task_main(); }, cfg, this);
    if (!task_->start())
    {
        running_ = false;
        task_.reset();
        EBOOK_LOGE(TAG, "task start failed");
        return false;
    }
    EBOOK_LOGI(TAG, "started");
    return true;
}

void InputRouter::stop()
{
    if (!running_) return;
    running_ = false;
    if (task_) { task_->destroy(); task_.reset(); }
}

void InputRouter::set_profile(Profile p)
{
    cfg_ = make_config(p);
    if (dev_ != nullptr) gesture_.init(dev_, &cfg_);
}

void InputRouter::drain_events()
{
    while (gesture_.has_event())
    {
        const Event ev = gesture_.take_event();
        (void)ui::UiBus::get_instance().post_input(ev);
    }
}

void InputRouter::process_release()
{
    if (!gesture_.is_pressed() || dev_->is_touched()) return;
    gesture_.process_sample({});
    drain_events();
}

void InputRouter::task_main()
{
    while (running_)
    {
        if (!dev_->wait_interrupt(kIdlePollMs))
        {
            gesture_.poll();
            gesture_.tick();
            drain_events();
            process_release();
            while (dev_->poll_interrupt()) {}
            continue;
        }

        // 触摸中合并中断，限制采样率
        do
        {
            gesture_.poll();
            drain_events();
            while (dev_->poll_interrupt())
            {
                gesture_.poll();
                drain_events();
            }
        } while (dev_->wait_interrupt(kCoalesceMs));

        gesture_.poll();
        drain_events();
        process_release();
        while (dev_->poll_interrupt()) {}
    }
}

} // namespace app::ebook::input
