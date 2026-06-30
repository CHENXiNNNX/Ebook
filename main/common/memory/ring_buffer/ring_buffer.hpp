#pragma once

#include <cstddef>
#include <cstdint>
#include <atomic>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace app::common::memory::ring_buffer {

struct Stats
{
    std::atomic<uint32_t> write_count{0};
    std::atomic<uint32_t> read_count{0};
    std::atomic<uint32_t> write_bytes{0};
    std::atomic<uint32_t> read_bytes{0};
    std::atomic<uint32_t> overflow_count{0};
    std::atomic<uint32_t> underflow_count{0};

    void reset()
    {
        write_count.store(0, std::memory_order_relaxed);
        read_count.store(0, std::memory_order_relaxed);
        write_bytes.store(0, std::memory_order_relaxed);
        read_bytes.store(0, std::memory_order_relaxed);
        overflow_count.store(0, std::memory_order_relaxed);
        underflow_count.store(0, std::memory_order_relaxed);
    }
};

/** @brief 线程安全环形缓冲，EnableStats 控制统计 */
template <bool EnableStats = false>
class RingBuffer
{
  public:
    explicit RingBuffer(size_t capacity, bool use_psram = true, size_t alignment = 4);
    RingBuffer(uint8_t* static_buffer, size_t capacity);

    ~RingBuffer();

    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;
    RingBuffer(RingBuffer&&) = delete;
    RingBuffer& operator=(RingBuffer&&) = delete;

    size_t write(const uint8_t* data, size_t len, int timeout_ms = 0);
    size_t read(uint8_t* data, size_t len, int timeout_ms = 0);
    size_t peek(uint8_t* data, size_t len) const;
    size_t skip(size_t len);
    void clear();

    bool write_byte(uint8_t byte);
    bool read_byte(uint8_t* byte);

    template <size_t N>
    size_t write_fixed(const uint8_t (&data)[N])
    {
        return write(data, N, 0);
    }

    template <size_t N>
    size_t read_fixed(uint8_t (&data)[N])
    {
        return read(data, N, 0);
    }

    size_t write_from_isr(const uint8_t* data, size_t len, BaseType_t* pxHigherPriorityTaskWoken);
    size_t read_from_isr(uint8_t* data, size_t len, BaseType_t* pxHigherPriorityTaskWoken);
    bool write_byte_from_isr(uint8_t byte, BaseType_t* pxHigherPriorityTaskWoken);
    bool read_byte_from_isr(uint8_t* byte, BaseType_t* pxHigherPriorityTaskWoken);

    size_t available_fast() const
    {
        return data_size_.load(std::memory_order_acquire);
    }
    size_t free_space_fast() const
    {
        return capacity_ - data_size_.load(std::memory_order_acquire);
    }

    size_t available() const;
    size_t free_space() const;

    size_t capacity() const
    {
        return capacity_;
    }
    bool is_empty() const
    {
        return available_fast() == 0;
    }
    bool is_full() const
    {
        return free_space_fast() == 0;
    }
    bool is_valid() const
    {
        return buffer_ != nullptr && mutex_ != nullptr;
    }
    bool is_static_buffer() const
    {
        return is_static_;
    }
    uint8_t* get_buffer()
    {
        return buffer_;
    }
    const uint8_t* get_buffer() const
    {
        return buffer_;
    }

    const Stats* get_stats() const
    {
        if constexpr (EnableStats)
            return &stats_;
        return nullptr;
    }

    void reset_stats()
    {
        if constexpr (EnableStats)
            stats_.reset();
    }

  private:
    void init_semaphores();
    void update_write_stats(size_t bytes);
    void update_read_stats(size_t bytes);
    void update_overflow();
    void update_underflow();

    uint8_t* buffer_{nullptr};
    size_t capacity_{0};
    std::atomic<size_t> read_pos_{0};
    std::atomic<size_t> write_pos_{0};
    std::atomic<size_t> data_size_{0};
    SemaphoreHandle_t mutex_{nullptr};
    SemaphoreHandle_t read_sem_{nullptr};
    SemaphoreHandle_t write_sem_{nullptr};
    bool is_static_{false};
    Stats stats_;
};

using RingBufferBasic = RingBuffer<false>;
using RingBufferWithStats = RingBuffer<true>;

} // namespace app::common::memory::ring_buffer

#include "ring_buffer_impl.hpp"
