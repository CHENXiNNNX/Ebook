#include "ebook_app.hpp"

#include "core/log.hpp"
#include "core/result.hpp"
#include "platform/boot.hpp"
#include "system/task/task.hpp"

static const char* const TAG = "EbookApp";

namespace {

void main_idle_loop()
{
    while (true)
        ::app::sys::task::TaskMgr::delay_ms(5000);
}

} // namespace

extern "C" void ebook_app_run(void)
{
    if (::app::ebook::core::ok(::app::ebook::platform::boot()))
        EBOOK_LOGI(TAG, "running");
    else
        EBOOK_LOGE(TAG, "boot failed");

    main_idle_loop();
}
