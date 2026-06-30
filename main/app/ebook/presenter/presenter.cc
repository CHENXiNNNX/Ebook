#include "presenter/presenter.hpp"

#include <cstring>

#include <esp_heap_caps.h>

#include "bsp/driver/gdey027t91/framebuffer.hpp"
#include "composer/composer.hpp"
#include "core/geometry.hpp"
#include "core/log.hpp"
#include "display/display_port.hpp"
#include "presenter/present_plan.hpp"
#include "router/refresh_intent.hpp"
#include "router/transition.hpp"

static const char* const TAG = "Presenter";

namespace app::ebook::presenter {

Presenter& Presenter::instance()
{
    static Presenter s;
    return s;
}

bool Presenter::init()
{
    if (back_fb_ != nullptr)
        return true;

    back_fb_ = static_cast<uint8_t*>(heap_caps_malloc(core::kFbBytes, MALLOC_CAP_8BIT));
    if (back_fb_ == nullptr)
    {
        EBOOK_LOGE(TAG, "alloc back_fb failed");
        return false;
    }
    std::memset(back_fb_, 0xFF, core::kFbBytes);

    queue_     = xQueueCreate(kQueueDepth, sizeof(FrameRequest));
    fb_mutex_  = xSemaphoreCreateMutex();
    state_evt_ = xEventGroupCreate();
    if (queue_ == nullptr || fb_mutex_ == nullptr || state_evt_ == nullptr)
    {
        deinit();
        return false;
    }
    xEventGroupSetBits(state_evt_, kBitIdle);
    return true;
}

void Presenter::deinit()
{
    stop();
    if (queue_ != nullptr) { vQueueDelete(queue_); queue_ = nullptr; }
    if (fb_mutex_ != nullptr) { vSemaphoreDelete(fb_mutex_); fb_mutex_ = nullptr; }
    if (state_evt_ != nullptr) { vEventGroupDelete(state_evt_); state_evt_ = nullptr; }
    if (back_fb_ != nullptr) { heap_caps_free(back_fb_); back_fb_ = nullptr; }
}

bool Presenter::start()
{
    if (started_)
        return true;
    if (back_fb_ == nullptr || queue_ == nullptr)
        return false;

    running_ = true;
    ::app::sys::task::Cfg cfg;
    cfg.name       = "ebook_present";
    cfg.stack_size = kStackBytes;
    cfg.priority   = kPriority;

    task_ = std::make_unique<::app::sys::task::Task>(
        [](void* arg) { static_cast<Presenter*>(arg)->task_main(); }, cfg, this);
    if (!task_->start())
    {
        running_ = false;
        task_.reset();
        return false;
    }
    started_ = true;
    EBOOK_LOGI(TAG, "started");
    return true;
}

void Presenter::stop()
{
    if (!started_)
        return;
    running_ = false;
    FrameRequest sentinel{};
    xQueueSend(queue_, &sentinel, pdMS_TO_TICKS(50));
    if (task_) { task_->destroy(); task_.reset(); }
    started_ = false;
}

uint8_t* Presenter::back_fb()
{
    return back_fb_;
}

bool Presenter::wait_idle(uint32_t timeout_ms)
{
    if (state_evt_ == nullptr)
        return false;
    const TickType_t to = (timeout_ms == portMAX_DELAY)
                              ? portMAX_DELAY
                              : pdMS_TO_TICKS(timeout_ms);
    const EventBits_t bits =
        xEventGroupWaitBits(state_evt_, kBitIdle, pdFALSE, pdTRUE, to);
    return (bits & kBitIdle) != 0;
}

void Presenter::copy_fb()
{
    uint8_t* front = display::DisplayPort::instance().framebuffer();
    if (front == nullptr || back_fb_ == nullptr)
        return;

    std::memcpy(front, back_fb_, core::kFbBytes);
}

core::Status Presenter::run_plan(const PresentPlan& plan)
{
    auto& port = display::DisplayPort::instance();
    core::Status last = core::Status::Ok;

    for (uint8_t i = 0; i < plan.count; ++i)
    {
        const PresentStep& s = plan.steps[i];
        switch (s.op)
        {
            case PresentOp::Bootstrap:
                last = port.bootstrap();
                break;
            case PresentOp::Partial:
                last = port.partial(s.rect);
                break;
            case PresentOp::Fast:
                last = port.fast();
                break;
            case PresentOp::Full:
                last = port.full();
                break;
        }
        if (last != core::Status::Ok)
            return last;
    }
    return last;
}

core::Status Presenter::execute(const FrameRequest& req)
{
    auto& port = display::DisplayPort::instance();
    const bool can_partial = (port.session() == display::SessionState::PartialReady);

    PresentPlan plan = make_plan(req.intent, can_partial);

    copy_fb();
    core::Status st = run_plan(plan);
    if (st == core::Status::Ok)
        return st;

    EBOOK_LOGW(TAG, "seq=%u present failed intent=%s",
               static_cast<unsigned>(req.seq),
               router::waveform_name(req.intent.waveform));

    if (req.intent.waveform != router::Waveform::Partial)
        return st;

    port.invalidate_session();
    plan = make_plan(req.intent, false);
    EBOOK_LOGW(TAG, "seq=%u partial failed, recover bootstrap", static_cast<unsigned>(req.seq));
    st = run_plan(plan);
    return st;
}

core::Status Presenter::submit(const FrameRequest& req)
{
    if (!started_)
        return core::Status::NotInit;

    (void)wait_idle(kSubmitTmoMs);

    if (xSemaphoreTake(fb_mutex_, pdMS_TO_TICKS(kSubmitTmoMs)) != pdTRUE)
        return core::Status::Timeout;

    composer::Composer::instance().paint(back_fb_);

    if (xQueueSend(queue_, &req, 0) != pdTRUE)
    {
        xSemaphoreGive(fb_mutex_);
        return core::Status::Timeout;
    }

    xEventGroupClearBits(state_evt_, kBitIdle);
    xSemaphoreGive(fb_mutex_);
    return core::Status::Ok;
}

void Presenter::task_main()
{
    FrameRequest req{};
    while (running_)
    {
        if (xQueueReceive(queue_, &req, portMAX_DELAY) != pdTRUE)
            continue;
        if (!running_)
            break;

        FrameRequest extra{};
        while (xQueueReceive(queue_, &extra, 0) == pdTRUE)
            req = extra;

        xSemaphoreTake(fb_mutex_, portMAX_DELAY);
        (void)execute(req);
        if (uxQueueMessagesWaiting(queue_) == 0)
            xEventGroupSetBits(state_evt_, kBitIdle);
        xSemaphoreGive(fb_mutex_);
    }
}

} // namespace app::ebook::presenter
