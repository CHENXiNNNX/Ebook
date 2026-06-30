#pragma once

#include <cstring>

#include "esp_heap_caps.h"
#include "esp_log.h"

namespace app::common::memory::double_buffer {

namespace {
static const char* const TAG = "DblBuf";
}

template <bool EnableStats>
DoubleBuffer<EnableStats>::DoubleBuffer(size_t buffer_size, bool use_psram, size_t alignment)
    : buffer_size_(buffer_size)
    , is_static_(false)
{
    states_[0].store(BufferState::EMPTY, std::memory_order_relaxed);
    states_[1].store(BufferState::EMPTY, std::memory_order_relaxed);
    data_sizes_[0].store(0, std::memory_order_relaxed);
    data_sizes_[1].store(0, std::memory_order_relaxed);

    if (buffer_size_ == 0)
        return;

    if (alignment == 0 || (alignment & (alignment - 1)) != 0)
        alignment = 4;

    buffer_size_ = (buffer_size_ + alignment - 1) & ~(alignment - 1);

    uint32_t caps = MALLOC_CAP_8BIT | (use_psram ? MALLOC_CAP_SPIRAM : MALLOC_CAP_INTERNAL);

    for (int i = 0; i < 2; i++)
    {
        buffers_[i] = static_cast<uint8_t*>(heap_caps_aligned_alloc(alignment, buffer_size_, caps));

        if (!buffers_[i] && use_psram)
        {
            caps = MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL;
            buffers_[i] =
                static_cast<uint8_t*>(heap_caps_aligned_alloc(alignment, buffer_size_, caps));
        }

        if (!buffers_[i])
        {
            ESP_LOGE(TAG, "alloc buf[%d] failed: %u bytes", i, (unsigned)buffer_size_);
            if (i == 1 && buffers_[0])
            {
                heap_caps_free(buffers_[0]);
                buffers_[0] = nullptr;
            }
            return;
        }
    }

    init_mutex();
}

template <bool EnableStats>
DoubleBuffer<EnableStats>::DoubleBuffer(uint8_t* buffer0, uint8_t* buffer1, size_t buffer_size)
    : buffer_size_(buffer_size)
    , is_static_(true)
{
    buffers_[0] = buffer0;
    buffers_[1] = buffer1;
    states_[0].store(BufferState::EMPTY, std::memory_order_relaxed);
    states_[1].store(BufferState::EMPTY, std::memory_order_relaxed);
    data_sizes_[0].store(0, std::memory_order_relaxed);
    data_sizes_[1].store(0, std::memory_order_relaxed);

    if (!buffers_[0] || !buffers_[1] || buffer_size_ == 0)
    {
        buffers_[0] = nullptr;
        buffers_[1] = nullptr;
        return;
    }

    init_mutex();
}

template <bool EnableStats>
void DoubleBuffer<EnableStats>::init_mutex()
{
    mutex_ = xSemaphoreCreateMutex();
    if (!mutex_)
    {
        ESP_LOGE(TAG, "mutex create failed");
        if (!is_static_)
        {
            if (buffers_[0])
                heap_caps_free(buffers_[0]);
            if (buffers_[1])
                heap_caps_free(buffers_[1]);
        }
        buffers_[0] = nullptr;
        buffers_[1] = nullptr;
    }
}

template <bool EnableStats>
DoubleBuffer<EnableStats>::~DoubleBuffer()
{
    if (mutex_)
        vSemaphoreDelete(mutex_);
    if (!is_static_)
    {
        if (buffers_[0])
            heap_caps_free(buffers_[0]);
        if (buffers_[1])
            heap_caps_free(buffers_[1]);
    }
}

// Statistics helpers
template <bool EnableStats>
inline void DoubleBuffer<EnableStats>::update_write_stats(size_t bytes)
{
    if constexpr (EnableStats)
    {
        stats_.write_count.fetch_add(1, std::memory_order_relaxed);
        stats_.write_bytes.fetch_add(bytes, std::memory_order_relaxed);
    }
}

template <bool EnableStats>
inline void DoubleBuffer<EnableStats>::update_read_stats(size_t bytes)
{
    if constexpr (EnableStats)
    {
        stats_.read_count.fetch_add(1, std::memory_order_relaxed);
        stats_.read_bytes.fetch_add(bytes, std::memory_order_relaxed);
    }
}

template <bool EnableStats>
inline void DoubleBuffer<EnableStats>::update_overflow()
{
    if constexpr (EnableStats)
        stats_.overflow_count.fetch_add(1, std::memory_order_relaxed);
}

template <bool EnableStats>
inline void DoubleBuffer<EnableStats>::update_underflow()
{
    if constexpr (EnableStats)
        stats_.underflow_count.fetch_add(1, std::memory_order_relaxed);
}

template <bool EnableStats>
int DoubleBuffer<EnableStats>::find_buffer(BufferState state) const
{
    int idx = write_index_.load(std::memory_order_relaxed);
    if (states_[idx].load(std::memory_order_acquire) == state)
        return idx;

    int other = 1 - idx;
    if (states_[other].load(std::memory_order_acquire) == state)
        return other;

    return -1;
}

// Acquire write buffer
template <bool EnableStats>
uint8_t* DoubleBuffer<EnableStats>::acquire_write_buffer()
{
    if (!is_valid())
        return nullptr;

    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE)
        return nullptr;

    int idx = find_buffer(BufferState::EMPTY);
    if (idx < 0)
    {
        xSemaphoreGive(mutex_);
        update_overflow();
        return nullptr;
    }

    states_[idx].store(BufferState::WRITING, std::memory_order_release);
    write_index_.store(idx, std::memory_order_relaxed);

    xSemaphoreGive(mutex_);
    return buffers_[idx];
}

// Release write buffer
template <bool EnableStats>
void DoubleBuffer<EnableStats>::release_write_buffer(size_t data_size)
{
    if (!is_valid())
        return;

    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE)
        return;

    int idx = write_index_.load(std::memory_order_relaxed);
    if (states_[idx].load(std::memory_order_acquire) == BufferState::WRITING)
    {
        data_sizes_[idx].store(data_size, std::memory_order_release);
        states_[idx].store(BufferState::READY, std::memory_order_release);
        update_write_stats(data_size);

        if constexpr (EnableStats)
            stats_.swap_count.fetch_add(1, std::memory_order_relaxed);
    }

    xSemaphoreGive(mutex_);
}

// Acquire read buffer
template <bool EnableStats>
uint8_t* DoubleBuffer<EnableStats>::acquire_read_buffer(size_t* data_size)
{
    if (!is_valid())
        return nullptr;

    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE)
        return nullptr;

    int idx = find_buffer(BufferState::READY);
    if (idx < 0)
    {
        xSemaphoreGive(mutex_);
        update_underflow();
        return nullptr;
    }

    states_[idx].store(BufferState::READING, std::memory_order_release);
    read_index_.store(idx, std::memory_order_relaxed);

    if (data_size)
        *data_size = data_sizes_[idx].load(std::memory_order_acquire);

    xSemaphoreGive(mutex_);
    return buffers_[idx];
}

// Release read buffer
template <bool EnableStats>
void DoubleBuffer<EnableStats>::release_read_buffer()
{
    if (!is_valid())
        return;

    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE)
        return;

    int idx = read_index_.load(std::memory_order_relaxed);
    if (states_[idx].load(std::memory_order_acquire) == BufferState::READING)
    {
        size_t bytes = data_sizes_[idx].load(std::memory_order_acquire);
        data_sizes_[idx].store(0, std::memory_order_release);
        states_[idx].store(BufferState::EMPTY, std::memory_order_release);
        update_read_stats(bytes);
    }

    xSemaphoreGive(mutex_);
}

// ISR: acquire write buffer
template <bool EnableStats>
uint8_t*
DoubleBuffer<EnableStats>::acquire_write_buffer_from_isr(BaseType_t* pxHigherPriorityTaskWoken)
{
    if (!is_valid())
        return nullptr;

    if (xSemaphoreTakeFromISR(mutex_, pxHigherPriorityTaskWoken) != pdTRUE)
        return nullptr;

    int idx = find_buffer(BufferState::EMPTY);
    if (idx < 0)
    {
        xSemaphoreGiveFromISR(mutex_, pxHigherPriorityTaskWoken);
        update_overflow();
        return nullptr;
    }

    states_[idx].store(BufferState::WRITING, std::memory_order_release);
    write_index_.store(idx, std::memory_order_relaxed);

    xSemaphoreGiveFromISR(mutex_, pxHigherPriorityTaskWoken);
    return buffers_[idx];
}

// ISR: release write buffer
template <bool EnableStats>
void DoubleBuffer<EnableStats>::release_write_buffer_from_isr(size_t data_size,
                                                              BaseType_t* pxHigherPriorityTaskWoken)
{
    if (!is_valid())
        return;

    if (xSemaphoreTakeFromISR(mutex_, pxHigherPriorityTaskWoken) != pdTRUE)
        return;

    int idx = write_index_.load(std::memory_order_relaxed);
    if (states_[idx].load(std::memory_order_acquire) == BufferState::WRITING)
    {
        data_sizes_[idx].store(data_size, std::memory_order_release);
        states_[idx].store(BufferState::READY, std::memory_order_release);
        update_write_stats(data_size);

        if constexpr (EnableStats)
            stats_.swap_count.fetch_add(1, std::memory_order_relaxed);
    }

    xSemaphoreGiveFromISR(mutex_, pxHigherPriorityTaskWoken);
}

// ISR: acquire read buffer
template <bool EnableStats>
uint8_t*
DoubleBuffer<EnableStats>::acquire_read_buffer_from_isr(size_t* data_size,
                                                        BaseType_t* pxHigherPriorityTaskWoken)
{
    if (!is_valid())
        return nullptr;

    if (xSemaphoreTakeFromISR(mutex_, pxHigherPriorityTaskWoken) != pdTRUE)
        return nullptr;

    int idx = find_buffer(BufferState::READY);
    if (idx < 0)
    {
        xSemaphoreGiveFromISR(mutex_, pxHigherPriorityTaskWoken);
        update_underflow();
        return nullptr;
    }

    states_[idx].store(BufferState::READING, std::memory_order_release);
    read_index_.store(idx, std::memory_order_relaxed);

    if (data_size)
        *data_size = data_sizes_[idx].load(std::memory_order_acquire);

    xSemaphoreGiveFromISR(mutex_, pxHigherPriorityTaskWoken);
    return buffers_[idx];
}

// ISR: release read buffer
template <bool EnableStats>
void DoubleBuffer<EnableStats>::release_read_buffer_from_isr(BaseType_t* pxHigherPriorityTaskWoken)
{
    if (!is_valid())
        return;

    if (xSemaphoreTakeFromISR(mutex_, pxHigherPriorityTaskWoken) != pdTRUE)
        return;

    int idx = read_index_.load(std::memory_order_relaxed);
    if (states_[idx].load(std::memory_order_acquire) == BufferState::READING)
    {
        size_t bytes = data_sizes_[idx].load(std::memory_order_acquire);
        data_sizes_[idx].store(0, std::memory_order_release);
        states_[idx].store(BufferState::EMPTY, std::memory_order_release);
        update_read_stats(bytes);
    }

    xSemaphoreGiveFromISR(mutex_, pxHigherPriorityTaskWoken);
}

// Convenience write (with copy)
template <bool EnableStats>
bool DoubleBuffer<EnableStats>::write(const uint8_t* data, size_t len)
{
    if (!data || len == 0 || len > buffer_size_)
        return false;

    uint8_t* buf = acquire_write_buffer();
    if (!buf)
        return false;

    std::memcpy(buf, data, len);
    release_write_buffer(len);
    return true;
}

// Convenience read (with copy)
template <bool EnableStats>
size_t DoubleBuffer<EnableStats>::read(uint8_t* data, size_t max_len)
{
    if (!data || max_len == 0)
        return 0;

    size_t size = 0;
    uint8_t* buf = acquire_read_buffer(&size);
    if (!buf)
        return 0;

    size_t to_read = (size < max_len) ? size : max_len;
    std::memcpy(data, buf, to_read);
    release_read_buffer();
    return to_read;
}

// Has data (locked)
template <bool EnableStats>
bool DoubleBuffer<EnableStats>::has_data() const
{
    if (!is_valid())
        return false;

    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE)
        return has_data_fast();

    bool result = states_[0].load(std::memory_order_acquire) == BufferState::READY ||
                  states_[1].load(std::memory_order_acquire) == BufferState::READY;

    xSemaphoreGive(mutex_);
    return result;
}

// Can write (locked)
template <bool EnableStats>
bool DoubleBuffer<EnableStats>::can_write() const
{
    if (!is_valid())
        return false;

    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE)
        return can_write_fast();

    bool result = states_[0].load(std::memory_order_acquire) == BufferState::EMPTY ||
                  states_[1].load(std::memory_order_acquire) == BufferState::EMPTY;

    xSemaphoreGive(mutex_);
    return result;
}

// Reset
template <bool EnableStats>
void DoubleBuffer<EnableStats>::reset()
{
    if (!is_valid())
        return;

    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE)
        return;

    states_[0].store(BufferState::EMPTY, std::memory_order_release);
    states_[1].store(BufferState::EMPTY, std::memory_order_release);
    data_sizes_[0].store(0, std::memory_order_release);
    data_sizes_[1].store(0, std::memory_order_release);
    write_index_.store(0, std::memory_order_release);
    read_index_.store(0, std::memory_order_release);

    xSemaphoreGive(mutex_);
}

} // namespace app::common::memory::double_buffer
