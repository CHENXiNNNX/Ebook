#include "info.hpp"

#include "esp_private/esp_clk.h"
#include "esp_heap_caps.h"
#include "esp_psram.h"

namespace app::sys::info {

MemoryInfo::MemoryInfo()
    : sram_total_(heap_caps_get_total_size(MALLOC_CAP_INTERNAL))
    , sram_free_(heap_caps_get_free_size(MALLOC_CAP_INTERNAL))
    , psram_total_(esp_psram_is_initialized() ? esp_psram_get_size() : 0)
    , psram_free_(esp_psram_is_initialized() ? heap_caps_get_free_size(MALLOC_CAP_SPIRAM) : 0)
{
}

MemoryInfo MemoryInfo::get_memory_info()
{
    return MemoryInfo();
}

CpuInfo::CpuInfo()
    : cpu_frequency_(static_cast<uint32_t>(esp_clk_cpu_freq()))
{
}

CpuInfo CpuInfo::get_cpu_info()
{
    return CpuInfo();
}

} // namespace app::sys::info
