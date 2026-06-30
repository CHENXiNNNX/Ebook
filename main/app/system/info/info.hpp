#pragma once

#include <cstddef>
#include <cstdint>

namespace app::sys::info {

/** SRAM / PSRAM 容量与空闲 */
class MemoryInfo
{
  public:
    static MemoryInfo get_memory_info();

    size_t get_sram_total() const { return sram_total_; }
    size_t get_sram_free() const { return sram_free_; }
    size_t get_psram_total() const { return psram_total_; }
    size_t get_psram_free() const { return psram_free_; }

  private:
    MemoryInfo();

    size_t sram_total_;
    size_t sram_free_;
    size_t psram_total_;
    size_t psram_free_;
};

/** CPU 主频（Hz） */
class CpuInfo
{
  public:
    static CpuInfo get_cpu_info();

    uint32_t get_cpu_freq() const { return cpu_frequency_; }

  private:
    CpuInfo();

    uint32_t cpu_frequency_;
};

} // namespace app::sys::info
