#pragma once

#include <cstring>

#include "esp_heap_caps.h"
#include "esp_log.h"

namespace app::common::memory::ring_buffer {

namespace {
static const char* const TAG = "RingBuf";
}

template <bool EnableStats>
RingBuffer<EnableStats>::RingBuffer(size_t capacity, bool use_psram, size_t alignment)
    : capacity_(capacity)
    , is_static_(false)
{
    if (capacity_ == 0)
        return;

    if (alignment == 0 || (alignment & (alignment - 1)) != 0)
        alignment = 4;

    capacity_ = (capacity_ + alignment - 1) & ~(alignment - 1);

    uint32_t caps = MALLOC_CAP_8BIT | (use_psram ? MALLOC_CAP_SPIRAM : MALLOC_CAP_INTERNAL);
    buffer_ = static_cast<uint8_t*>(heap_caps_aligned_alloc(alignment, capacity_, caps));

    if (!buffer_ && use_psram)
    {
        caps = MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL;
        buffer_ = static_cast<uint8_t*>(heap_caps_aligned_alloc(alignment, capacity_, caps));
    }

    if (!buffer_)
    {
        ESP_LOGE(TAG, "alloc failed: %u bytes", (unsigned)capacity_);
        return;
    }

    init_semaphores();
}

template <bool EnableStats>
RingBuffer<EnableStats>::RingBuffer(uint8_t* static_buffer, size_t capacity)
    : buffer_(static_buffer)
    , capacity_(capacity)
    , is_static_(true)
{
    if (!buffer_ || capacity_ == 0)
    {
        buffer_ = nullptr;
        return;
    }
    init_semaphores();
}

template <bool EnableStats>
void RingBuffer<EnableStats>::init_semaphores()
{
    mutex_ = xSemaphoreCreateMutex();
    if (!mutex_)
    {
        ESP_LOGE(TAG, "mutex create failed");
        if (!is_static_ && buffer_)
        {
            heap_caps_free(buffer_);
            buffer_ = nullptr;
        }
        return;
    }

    read_sem_ = xSemaphoreCreateBinary();
    write_sem_ = xSemaphoreCreateBinary();

    if (!read_sem_ || !write_sem_)
    {
        ESP_LOGE(TAG, "semaphore create failed");
        if (read_sem_)
            vSemaphoreDelete(read_sem_);
        if (write_sem_)
            vSemaphoreDelete(write_sem_);
        vSemaphoreDelete(mutex_);
        if (!is_static_ && buffer_)
            heap_caps_free(buffer_);
        buffer_ = nullptr;
        mutex_ = nullptr;
        read_sem_ = nullptr;
        write_sem_ = nullptr;
        return;
    }

    xSemaphoreGive(write_sem_);
}

template <bool EnableStats>
RingBuffer<EnableStats>::~RingBuffer()
{
    if (read_sem_)
        vSemaphoreDelete(read_sem_);
    if (write_sem_)
        vSemaphoreDelete(write_sem_);
    if (mutex_)
        vSemaphoreDelete(mutex_);
    if (!is_static_ && buffer_)
        heap_caps_free(buffer_);
}

// Statistics helpers
template <bool EnableStats>
inline void RingBuffer<EnableStats>::update_write_stats(size_t bytes)
{
    if constexpr (EnableStats)
    {
        stats_.write_count.fetch_add(1, std::memory_order_relaxed);
        stats_.write_bytes.fetch_add(bytes, std::memory_order_relaxed);
    }
}

template <bool EnableStats>
inline void RingBuffer<EnableStats>::update_read_stats(size_t bytes)
{
    if constexpr (EnableStats)
    {
        stats_.read_count.fetch_add(1, std::memory_order_relaxed);
        stats_.read_bytes.fetch_add(bytes, std::memory_order_relaxed);
    }
}

template <bool EnableStats>
inline void RingBuffer<EnableStats>::update_overflow()
{
    if constexpr (EnableStats)
        stats_.overflow_count.fetch_add(1, std::memory_order_relaxed);
}

template <bool EnableStats>
inline void RingBuffer<EnableStats>::update_underflow()
{
    if constexpr (EnableStats)
        stats_.underflow_count.fetch_add(1, std::memory_order_relaxed);
}

// Write
template <bool EnableStats>
size_t RingBuffer<EnableStats>::write(const uint8_t* data, size_t len, int timeout_ms)
{
    if (!is_valid() || !data || len == 0)
        return 0;

    if (timeout_ms == 0 && free_space_fast() == 0)
    {
        update_overflow();
        return 0;
    }

    if (timeout_ms != 0)
    {
        TickType_t ticks = (timeout_ms < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
        if (xSemaphoreTake(write_sem_, ticks) != pdTRUE)
        {
            update_overflow();
            return 0;
        }
    }

    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE)
    {
        if (timeout_ms != 0)
            xSemaphoreGive(write_sem_);
        return 0;
    }

    size_t current = data_size_.load(std::memory_order_relaxed);
    size_t free = capacity_ - current;
    size_t to_write = (len < free) ? len : free;

    if (to_write > 0)
    {
        size_t wp = write_pos_.load(std::memory_order_relaxed);
        size_t first = capacity_ - wp;

        if (to_write <= first)
        {
            std::memcpy(buffer_ + wp, data, to_write);
        }
        else
        {
            std::memcpy(buffer_ + wp, data, first);
            std::memcpy(buffer_, data + first, to_write - first);
        }

        write_pos_.store((wp + to_write) % capacity_, std::memory_order_release);
        data_size_.fetch_add(to_write, std::memory_order_release);

        update_write_stats(to_write);
        xSemaphoreGive(read_sem_);
    }

    if (data_size_.load(std::memory_order_acquire) < capacity_)
        xSemaphoreGive(write_sem_);

    xSemaphoreGive(mutex_);
    return to_write;
}

// Read
template <bool EnableStats>
size_t RingBuffer<EnableStats>::read(uint8_t* data, size_t len, int timeout_ms)
{
    if (!is_valid() || !data || len == 0)
        return 0;

    if (timeout_ms == 0 && available_fast() == 0)
    {
        update_underflow();
        return 0;
    }

    if (timeout_ms != 0)
    {
        TickType_t ticks = (timeout_ms < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
        if (xSemaphoreTake(read_sem_, ticks) != pdTRUE)
        {
            update_underflow();
            return 0;
        }
    }

    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE)
    {
        if (timeout_ms != 0)
            xSemaphoreGive(read_sem_);
        return 0;
    }

    size_t current = data_size_.load(std::memory_order_relaxed);
    size_t to_read = (len < current) ? len : current;

    if (to_read > 0)
    {
        size_t rp = read_pos_.load(std::memory_order_relaxed);
        size_t first = capacity_ - rp;

        if (to_read <= first)
        {
            std::memcpy(data, buffer_ + rp, to_read);
        }
        else
        {
            std::memcpy(data, buffer_ + rp, first);
            std::memcpy(data + first, buffer_, to_read - first);
        }

        read_pos_.store((rp + to_read) % capacity_, std::memory_order_release);
        data_size_.fetch_sub(to_read, std::memory_order_release);

        update_read_stats(to_read);
        xSemaphoreGive(write_sem_);
    }

    if (data_size_.load(std::memory_order_acquire) > 0)
        xSemaphoreGive(read_sem_);

    xSemaphoreGive(mutex_);
    return to_read;
}

// Single byte write
template <bool EnableStats>
bool RingBuffer<EnableStats>::write_byte(uint8_t byte)
{
    if (!is_valid() || free_space_fast() == 0)
    {
        update_overflow();
        return false;
    }

    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(10)) != pdTRUE)
        return false;

    if (data_size_.load(std::memory_order_relaxed) >= capacity_)
    {
        xSemaphoreGive(mutex_);
        update_overflow();
        return false;
    }

    size_t wp = write_pos_.load(std::memory_order_relaxed);
    buffer_[wp] = byte;
    write_pos_.store((wp + 1) % capacity_, std::memory_order_release);
    data_size_.fetch_add(1, std::memory_order_release);

    update_write_stats(1);
    xSemaphoreGive(read_sem_);

    if (data_size_.load(std::memory_order_acquire) < capacity_)
        xSemaphoreGive(write_sem_);

    xSemaphoreGive(mutex_);
    return true;
}

// Single byte read
template <bool EnableStats>
bool RingBuffer<EnableStats>::read_byte(uint8_t* byte)
{
    if (!is_valid() || !byte || available_fast() == 0)
    {
        update_underflow();
        return false;
    }

    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(10)) != pdTRUE)
        return false;

    if (data_size_.load(std::memory_order_relaxed) == 0)
    {
        xSemaphoreGive(mutex_);
        update_underflow();
        return false;
    }

    size_t rp = read_pos_.load(std::memory_order_relaxed);
    *byte = buffer_[rp];
    read_pos_.store((rp + 1) % capacity_, std::memory_order_release);
    data_size_.fetch_sub(1, std::memory_order_release);

    update_read_stats(1);
    xSemaphoreGive(write_sem_);

    if (data_size_.load(std::memory_order_acquire) > 0)
        xSemaphoreGive(read_sem_);

    xSemaphoreGive(mutex_);
    return true;
}

// ISR write
template <bool EnableStats>
size_t RingBuffer<EnableStats>::write_from_isr(const uint8_t* data, size_t len,
                                               BaseType_t* pxHigherPriorityTaskWoken)
{
    if (!is_valid() || !data || len == 0)
        return 0;

    if (xSemaphoreTakeFromISR(mutex_, pxHigherPriorityTaskWoken) != pdTRUE)
        return 0;

    size_t current = data_size_.load(std::memory_order_relaxed);
    size_t free = capacity_ - current;
    size_t to_write = (len < free) ? len : free;

    if (to_write > 0)
    {
        size_t wp = write_pos_.load(std::memory_order_relaxed);
        size_t first = capacity_ - wp;

        if (to_write <= first)
            std::memcpy(buffer_ + wp, data, to_write);
        else
        {
            std::memcpy(buffer_ + wp, data, first);
            std::memcpy(buffer_, data + first, to_write - first);
        }

        write_pos_.store((wp + to_write) % capacity_, std::memory_order_release);
        data_size_.fetch_add(to_write, std::memory_order_release);

        update_write_stats(to_write);
        xSemaphoreGiveFromISR(read_sem_, pxHigherPriorityTaskWoken);
    }
    else
    {
        update_overflow();
    }

    if (data_size_.load(std::memory_order_acquire) < capacity_)
        xSemaphoreGiveFromISR(write_sem_, pxHigherPriorityTaskWoken);

    xSemaphoreGiveFromISR(mutex_, pxHigherPriorityTaskWoken);
    return to_write;
}

// ISR read
template <bool EnableStats>
size_t RingBuffer<EnableStats>::read_from_isr(uint8_t* data, size_t len,
                                              BaseType_t* pxHigherPriorityTaskWoken)
{
    if (!is_valid() || !data || len == 0)
        return 0;

    if (xSemaphoreTakeFromISR(mutex_, pxHigherPriorityTaskWoken) != pdTRUE)
        return 0;

    size_t current = data_size_.load(std::memory_order_relaxed);
    size_t to_read = (len < current) ? len : current;

    if (to_read > 0)
    {
        size_t rp = read_pos_.load(std::memory_order_relaxed);
        size_t first = capacity_ - rp;

        if (to_read <= first)
            std::memcpy(data, buffer_ + rp, to_read);
        else
        {
            std::memcpy(data, buffer_ + rp, first);
            std::memcpy(data + first, buffer_, to_read - first);
        }

        read_pos_.store((rp + to_read) % capacity_, std::memory_order_release);
        data_size_.fetch_sub(to_read, std::memory_order_release);

        update_read_stats(to_read);
        xSemaphoreGiveFromISR(write_sem_, pxHigherPriorityTaskWoken);
    }
    else
    {
        update_underflow();
    }

    if (data_size_.load(std::memory_order_acquire) > 0)
        xSemaphoreGiveFromISR(read_sem_, pxHigherPriorityTaskWoken);

    xSemaphoreGiveFromISR(mutex_, pxHigherPriorityTaskWoken);
    return to_read;
}

// ISR single byte write
template <bool EnableStats>
bool RingBuffer<EnableStats>::write_byte_from_isr(uint8_t byte,
                                                  BaseType_t* pxHigherPriorityTaskWoken)
{
    if (!is_valid())
        return false;

    if (xSemaphoreTakeFromISR(mutex_, pxHigherPriorityTaskWoken) != pdTRUE)
        return false;

    if (data_size_.load(std::memory_order_relaxed) >= capacity_)
    {
        xSemaphoreGiveFromISR(mutex_, pxHigherPriorityTaskWoken);
        update_overflow();
        return false;
    }

    size_t wp = write_pos_.load(std::memory_order_relaxed);
    buffer_[wp] = byte;
    write_pos_.store((wp + 1) % capacity_, std::memory_order_release);
    data_size_.fetch_add(1, std::memory_order_release);

    update_write_stats(1);
    xSemaphoreGiveFromISR(read_sem_, pxHigherPriorityTaskWoken);

    if (data_size_.load(std::memory_order_acquire) < capacity_)
        xSemaphoreGiveFromISR(write_sem_, pxHigherPriorityTaskWoken);

    xSemaphoreGiveFromISR(mutex_, pxHigherPriorityTaskWoken);
    return true;
}

// ISR single byte read
template <bool EnableStats>
bool RingBuffer<EnableStats>::read_byte_from_isr(uint8_t* byte,
                                                 BaseType_t* pxHigherPriorityTaskWoken)
{
    if (!is_valid() || !byte)
        return false;

    if (xSemaphoreTakeFromISR(mutex_, pxHigherPriorityTaskWoken) != pdTRUE)
        return false;

    if (data_size_.load(std::memory_order_relaxed) == 0)
    {
        xSemaphoreGiveFromISR(mutex_, pxHigherPriorityTaskWoken);
        update_underflow();
        return false;
    }

    size_t rp = read_pos_.load(std::memory_order_relaxed);
    *byte = buffer_[rp];
    read_pos_.store((rp + 1) % capacity_, std::memory_order_release);
    data_size_.fetch_sub(1, std::memory_order_release);

    update_read_stats(1);
    xSemaphoreGiveFromISR(write_sem_, pxHigherPriorityTaskWoken);

    if (data_size_.load(std::memory_order_acquire) > 0)
        xSemaphoreGiveFromISR(read_sem_, pxHigherPriorityTaskWoken);

    xSemaphoreGiveFromISR(mutex_, pxHigherPriorityTaskWoken);
    return true;
}

// Peek
template <bool EnableStats>
size_t RingBuffer<EnableStats>::peek(uint8_t* data, size_t len) const
{
    if (!is_valid() || !data || len == 0)
        return 0;

    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE)
        return 0;

    size_t current = data_size_.load(std::memory_order_relaxed);
    size_t to_peek = (len < current) ? len : current;

    if (to_peek > 0)
    {
        size_t rp = read_pos_.load(std::memory_order_relaxed);
        size_t first = capacity_ - rp;

        if (to_peek <= first)
            std::memcpy(data, buffer_ + rp, to_peek);
        else
        {
            std::memcpy(data, buffer_ + rp, first);
            std::memcpy(data + first, buffer_, to_peek - first);
        }
    }

    xSemaphoreGive(mutex_);
    return to_peek;
}

// Skip
template <bool EnableStats>
size_t RingBuffer<EnableStats>::skip(size_t len)
{
    if (!is_valid() || len == 0)
        return 0;

    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE)
        return 0;

    size_t current = data_size_.load(std::memory_order_relaxed);
    size_t to_skip = (len < current) ? len : current;

    if (to_skip > 0)
    {
        size_t rp = read_pos_.load(std::memory_order_relaxed);
        read_pos_.store((rp + to_skip) % capacity_, std::memory_order_release);
        data_size_.fetch_sub(to_skip, std::memory_order_release);
        xSemaphoreGive(write_sem_);
    }

    if (data_size_.load(std::memory_order_acquire) > 0)
        xSemaphoreGive(read_sem_);

    xSemaphoreGive(mutex_);
    return to_skip;
}

// Clear
template <bool EnableStats>
void RingBuffer<EnableStats>::clear()
{
    if (!is_valid())
        return;

    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE)
        return;

    read_pos_.store(0, std::memory_order_release);
    write_pos_.store(0, std::memory_order_release);
    data_size_.store(0, std::memory_order_release);

    xSemaphoreTake(read_sem_, 0);
    xSemaphoreGive(write_sem_);
    xSemaphoreGive(mutex_);
}

// Available (locked)
template <bool EnableStats>
size_t RingBuffer<EnableStats>::available() const
{
    if (!is_valid())
        return 0;

    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE)
        return available_fast();

    size_t result = data_size_.load(std::memory_order_acquire);
    xSemaphoreGive(mutex_);
    return result;
}

// Free space (locked)
template <bool EnableStats>
size_t RingBuffer<EnableStats>::free_space() const
{
    if (!is_valid())
        return 0;

    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE)
        return free_space_fast();

    size_t result = capacity_ - data_size_.load(std::memory_order_acquire);
    xSemaphoreGive(mutex_);
    return result;
}

} // namespace app::common::memory::ring_buffer
