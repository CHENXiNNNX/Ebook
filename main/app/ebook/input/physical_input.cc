#include "input/physical_input.hpp"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "core/log.hpp"
#include "input/physical_types.hpp"
#include "ui/ui_bus.hpp"

static const char* const TAG = "PhysicalInput";

namespace app::ebook::input {

namespace {

using ::app::bsp::driver::button::DipId;

uint32_t now_ms()
{
    return static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

PhysicalKey map_key(DipId id)
{
    switch (id)
    {
        case DipId::UP:   return PhysicalKey::Up;
        case DipId::MID:  return PhysicalKey::Mid;
        case DipId::DOWN: return PhysicalKey::Down;
        case DipId::NONE:
            break;
    }
    return PhysicalKey::Mid;
}

void post_physical(PhysicalKey key, PhysicalAction action)
{
    (void)ui::UiBus::get_instance().post_physical(key, action);
}

} // namespace

PhysicalInput& PhysicalInput::get_instance()
{
    static PhysicalInput s;
    return s;
}

bool PhysicalInput::init()
{
    if (dip_.is_init())
        return true;
    if (!dip_.init())
    {
        EBOOK_LOGW(TAG, "dip init failed");
        return false;
    }
    stable_        = DipId::NONE;
    pending_       = DipId::NONE;
    pending_count_ = 0;
    last_emit_ms_  = now_ms();
    return true;
}

void PhysicalInput::deinit()
{
    stop();
    dip_.deinit();
}

bool PhysicalInput::start()
{
    if (running_)
        return true;
    if (!dip_.is_init())
        return false;

    ::app::sys::task::Cfg cfg;
    cfg.name       = "ebook_dip";
    cfg.stack_size = kStackBytes;
    cfg.priority   = ::app::sys::task::Priority::NORMAL;
    cfg.core_id    = tskNO_AFFINITY;

    running_ = true;
    task_    = std::make_unique<::app::sys::task::Task>(
        [](void* arg) { static_cast<PhysicalInput*>(arg)->task_main(); }, cfg, this);
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

void PhysicalInput::stop()
{
    if (!running_)
        return;
    running_ = false;
    if (task_)
    {
        task_->destroy();
        task_.reset();
    }
}

void PhysicalInput::poll_once()
{
    DipId raw = DipId::NONE;
    if (dip_.read(DipId::UP))
        raw = DipId::UP;
    else if (dip_.read(DipId::MID))
        raw = DipId::MID;
    else if (dip_.read(DipId::DOWN))
        raw = DipId::DOWN;

    if (raw == stable_)
    {
        pending_       = DipId::NONE;
        pending_count_ = 0;
        return;
    }

    if (raw == pending_)
    {
        if (pending_count_ < 255)
            ++pending_count_;
    }
    else
    {
        pending_       = raw;
        pending_count_ = 1;
    }

    if (pending_count_ < kDebounceSamples)
        return;

    const DipId prev = stable_;
    stable_          = pending_;
    pending_count_   = 0;

    const uint32_t t = now_ms();
    if (t - last_emit_ms_ < kCooldownMs)
        return;

    if (stable_ != DipId::NONE)
    {
        EBOOK_LOGD(TAG, "press %s", physical_key_name(map_key(stable_)));
        post_physical(map_key(stable_), PhysicalAction::Press);
        last_emit_ms_ = t;
        return;
    }

    if (prev != DipId::NONE)
    {
        EBOOK_LOGD(TAG, "release %s", physical_key_name(map_key(prev)));
        post_physical(map_key(prev), PhysicalAction::Release);
        last_emit_ms_ = t;
    }
}

void PhysicalInput::task_main()
{
    while (running_)
    {
        poll_once();
        ::app::sys::task::TaskMgr::delay_ms(kPollMs);
    }
}

} // namespace app::ebook::input
