#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <unordered_map>
#include <vector>

#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace app::common::memory {

/** @brief heap_caps_free 删除器，供 unique_ptr 使用 */
struct EspHeapDeleter
{
    void operator()(uint8_t* ptr) const
    {
        if (ptr != nullptr)
        {
            heap_caps_free(ptr);
        }
    }
};

/** @brief FreeRTOS 互斥锁 RAII */
class MutexRAII
{
  public:
    static MutexRAII create()
    {
        SemaphoreHandle_t handle = xSemaphoreCreateMutex();
        return MutexRAII(handle);
    }

    MutexRAII(MutexRAII&& other) noexcept
        : handle_(other.handle_)
    {
        other.handle_ = nullptr;
    }

    MutexRAII& operator=(MutexRAII&& other) noexcept
    {
        if (this != &other)
        {
            reset();
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    MutexRAII(const MutexRAII&) = delete;
    MutexRAII& operator=(const MutexRAII&) = delete;

    ~MutexRAII()
    {
        reset();
    }

    SemaphoreHandle_t get() const
    {
        return handle_;
    }

    bool is_valid() const
    {
        return handle_ != nullptr;
    }

    operator SemaphoreHandle_t() const
    {
        return handle_;
    }

    void reset()
    {
        if (handle_ != nullptr)
        {
            vSemaphoreDelete(handle_);
            handle_ = nullptr;
        }
    }

  private:
    explicit MutexRAII(SemaphoreHandle_t handle)
        : handle_(handle)
    {
    }

    SemaphoreHandle_t handle_;
};

/** @brief 互斥锁作用域守卫 */
class MutexLockGuard
{
  public:
    explicit MutexLockGuard(SemaphoreHandle_t mutex, TickType_t timeout = portMAX_DELAY)
        : mutex_(mutex)
        , locked_(false)
    {
        if (mutex_ != nullptr)
        {
            locked_ = (xSemaphoreTake(mutex_, timeout) == pdTRUE);
        }
    }

    ~MutexLockGuard()
    {
        release();
    }

    MutexLockGuard(const MutexLockGuard&) = delete;
    MutexLockGuard& operator=(const MutexLockGuard&) = delete;

    bool is_locked() const
    {
        return locked_;
    }

    void release()
    {
        if (locked_ && mutex_ != nullptr)
        {
            xSemaphoreGive(mutex_);
            locked_ = false;
        }
    }

  private:
    SemaphoreHandle_t mutex_;
    bool locked_;
};

constexpr size_t BYTES_PER_KILOBYTE = 1024;
constexpr size_t BYTES_PER_MEGABYTE = BYTES_PER_KILOBYTE * 1024;
constexpr size_t DEFAULT_INITIAL_POOL_SIZE = 64 * BYTES_PER_KILOBYTE;
constexpr double DEFAULT_EXPANSION_FACTOR = 2.0;

/** @brief 内部 SRAM 优先的堆内存池 */
class MemoryPool
{
  public:
    struct Stats
    {
        size_t total_memory;
        size_t used_memory;
        size_t free_memory;
        size_t allocated_blocks;
        size_t free_blocks;
    };

    explicit MemoryPool(size_t initial_size = DEFAULT_INITIAL_POOL_SIZE,
                        size_t alignment = alignof(std::max_align_t),
                        double expansion_factor = DEFAULT_EXPANSION_FACTOR);

    ~MemoryPool();

    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;

    void* allocate(size_t size);
    void deallocate(void* ptr);
    void reset();
    Stats get_stats() const;

  private:
    struct BlockHeader
    {
        size_t size;
        bool is_free;
        BlockHeader* next;
        BlockHeader* prev;
    };

    struct PoolBlock
    {
        std::unique_ptr<uint8_t[], EspHeapDeleter> memory;
        size_t size;
        BlockHeader* first_block;
    };

    MutexRAII mutex_;
    size_t alignment_;
    double expansion_factor_;
    std::vector<PoolBlock> pool_blocks_;
    std::unordered_map<void*, size_t> pointer_map_;
    mutable std::multimap<size_t, void*> free_blocks_by_size_;
    mutable std::unordered_map<void*, std::multimap<size_t, void*>::iterator>
        free_blocks_iterators_;

    void init_pool(size_t size);
    void expand_pool(size_t required_size);
    BlockHeader* find_free_block(size_t size);
    void split_block(BlockHeader* block, size_t size);
    void coalesce_blocks(BlockHeader* block);
    size_t aligned_size(size_t size) const;
    size_t get_header_size() const;
};

} // namespace app::common::memory
