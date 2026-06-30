#pragma once

#include <cstddef>
#include <cstdint>
#include <atomic>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace app::common::memory::double_buffer {

enum class BufferState : uint8_t
{
    EMPTY,
    WRITING,
    READY,
    READING
};

struct Stats
{
    std::atomic<uint32_t> write_count{0};
    std::atomic<uint32_t> read_count{0};
    std::atomic<uint32_t> swap_count{0};
    std::atomic<uint32_t> write_bytes{0};
    std::atomic<uint32_t> read_bytes{0};
    std::atomic<uint32_t> overflow_count{0};
    std::atomic<uint32_t> underflow_count{0};

    void reset()
    {
        write_count.store(0, std::memory_order_relaxed);
        read_count.store(0, std::memory_order_relaxed);
        swap_count.store(0, std::memory_order_relaxed);
        write_bytes.store(0, std::memory_order_relaxed);
        read_bytes.store(0, std::memory_order_relaxed);
        overflow_count.store(0, std::memory_order_relaxed);
        underflow_count.store(0, std::memory_order_relaxed);
    }
};

/** @brief 零拷贝双缓冲，EnableStats 控制统计 */
template <bool EnableStats = false>
class DoubleBuffer
{
  public:
    explicit DoubleBuffer(size_t buffer_size, bool use_psram = true, size_t alignment = 4);
    DoubleBuffer(uint8_t* buffer0, uint8_t* buffer1, size_t buffer_size);

    ~DoubleBuffer();

    DoubleBuffer(const DoubleBuffer&) = delete;
    DoubleBuffer& operator=(const DoubleBuffer&) = delete;
    DoubleBuffer(DoubleBuffer&&) = delete;
    DoubleBuffer& operator=(DoubleBuffer&&) = delete;

    uint8_t* acquire_write_buffer();
    void release_write_buffer(size_t data_size);
    uint8_t* acquire_read_buffer(size_t* data_size);
    void release_read_buffer();

    uint8_t* acquire_write_buffer_from_isr(BaseType_t* pxHigherPriorityTaskWoken);
    void release_write_buffer_from_isr(size_t data_size, BaseType_t* pxHigherPriorityTaskWoken);
    uint8_t* acquire_read_buffer_from_isr(size_t* data_size, BaseType_t* pxHigherPriorityTaskWoken);
    void release_read_buffer_from_isr(BaseType_t* pxHigherPriorityTaskWoken);

    bool write(const uint8_t* data, size_t len);
    size_t read(uint8_t* data, size_t max_len);

    bool has_data_fast() const
    {
        return states_[0].load(std::memory_order_acquire) == BufferState::READY ||
               states_[1].load(std::memory_order_acquire) == BufferState::READY;
    }

    bool can_write_fast() const
    {
        return states_[0].load(std::memory_order_acquire) == BufferState::EMPTY ||
               states_[1].load(std::memory_order_acquire) == BufferState::EMPTY;
    }

    bool has_data() const;
    bool can_write() const;

    size_t get_buffer_size() const
    {
        return buffer_size_;
    }
    bool is_valid() const
    {
        return buffers_[0] && buffers_[1] && mutex_;
    }
    bool is_static_buffer() const
    {
        return is_static_;
    }
    BufferState get_buffer_state(int i) const
    {
        return (i < 0 || i > 1) ? BufferState::EMPTY : states_[i].load(std::memory_order_acquire);
    }
    size_t get_data_size(int i) const
    {
        return (i < 0 || i > 1) ? 0 : data_sizes_[i].load(std::memory_order_acquire);
    }
    uint8_t* get_buffer(int i)
    {
        return (i < 0 || i > 1) ? nullptr : buffers_[i];
    }
    const uint8_t* get_buffer(int i) const
    {
        return (i < 0 || i > 1) ? nullptr : buffers_[i];
    }

    void reset();

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
    void init_mutex();
    int find_buffer(BufferState state) const;
    void update_write_stats(size_t bytes);
    void update_read_stats(size_t bytes);
    void update_overflow();
    void update_underflow();

    uint8_t* buffers_[2]{nullptr, nullptr};
    size_t buffer_size_{0};
    std::atomic<BufferState> states_[2];
    std::atomic<size_t> data_sizes_[2];
    std::atomic<int> write_index_{0};
    std::atomic<int> read_index_{0};
    SemaphoreHandle_t mutex_{nullptr};
    bool is_static_{false};
    Stats stats_;
};

using DoubleBufferBasic = DoubleBuffer<false>;
using DoubleBufferWithStats = DoubleBuffer<true>;

} // namespace app::common::memory::double_buffer

#include "double_buffer_impl.hpp"
