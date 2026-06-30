#include "platform/housekeeper.hpp"

#include "apps/settings/auto_lock.hpp"
#include "core/log.hpp"
#include "data/clock_provider.hpp"
#include "apps/clock/clock_store.hpp"
#include "data/system_state.hpp"
#include "platform/battery_sampler.hpp"
#include "storage/storage.hpp"
#include "ui/ui_bus.hpp"

static const char* const TAG = "Housekeeper";

namespace app::ebook::platform {

Housekeeper& Housekeeper::get_instance()
{
    static Housekeeper s;
    return s;
}

bool Housekeeper::start()
{
    if (running_) return true;

    ::app::sys::task::Cfg cfg;
    cfg.name       = "ebook_house";
    cfg.stack_size = kStackBytes;
    cfg.priority   = ::app::sys::task::Priority::LOW;
    cfg.core_id    = tskNO_AFFINITY;

    running_ = true;
    task_ = std::make_unique<::app::sys::task::Task>(
        [](void* arg) { static_cast<Housekeeper*>(arg)->run(); }, cfg, this);
    if (!task_->start())
    {
        running_ = false;
        task_.reset();
        EBOOK_LOGE(TAG, "task start failed");
        return false;
    }
    return true;
}

void Housekeeper::stop()
{
    if (!running_) return;
    running_ = false;
    if (task_) { task_->destroy(); task_.reset(); }
}

void Housekeeper::run()
{
    uint8_t  last_hour = 0xFF;
    uint8_t  last_min  = 0xFF;
    uint8_t  last_pct  = data::SystemState::get_instance().battery_pct();
    uint32_t tick      = 0;

    auto broadcast_clock_if_changed = [&](const data::Clock& clk) {
        apps::clock::ClockStore::get_instance().tick(clk.hour, clk.minute, clk.second, clk.weekday);
        if (clk.hour != last_hour || clk.minute != last_min)
        {
            last_hour = clk.hour;
            last_min  = clk.minute;
            (void)ui::UiBus::get_instance().post_tick_clock(clk.hour, clk.minute, 255);
        }
    };

    broadcast_clock_if_changed(data::Clock::now());

    while (running_)
    {
        ::app::sys::task::TaskMgr::delay_ms(kPeriodMs);
        ++tick;

        broadcast_clock_if_changed(data::Clock::now());

        if ((tick % kBatteryEveryTicks) == 0 && BatterySampler::get_instance().ready())
        {
            const uint8_t pct = BatterySampler::get_instance().percent();
            if (pct != last_pct)
            {
                last_pct = pct;
                (void)ui::UiBus::get_instance().post_tick_battery(pct,
                    BatterySampler::get_instance().voltage_mv());
            }
        }

        apps::settings::AutoLock::get_instance().tick();

        ::app::common::storage::StorageMgr::get_instance().service_sd_hotplug();
        ::app::common::storage::StorageMgr::get_instance().service_internal_recovery();
    }
}

} // namespace app::ebook::platform
