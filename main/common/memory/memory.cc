#include "memory.hpp"

#include <algorithm>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace app::common::memory {

MemoryPool::MemoryPool(size_t initial_size, size_t alignment, double expansion_factor)
    : mutex_(MutexRAII::create())
    , alignment_(alignment)
    , expansion_factor_(expansion_factor)
{
    if (!mutex_.is_valid())
    {
        alignment_ = alignof(std::max_align_t);
        return;
    }

    if (alignment_ == 0 || (alignment_ & (alignment_ - 1)) != 0)
    {
        alignment_ = alignof(std::max_align_t);
    }

    if (expansion_factor_ < 1.0)
    {
        expansion_factor_ = 1.0;
    }

    init_pool(initial_size);
}

MemoryPool::~MemoryPool()
{
    reset();
}

void MemoryPool::init_pool(size_t size)
{
    size_t aligned_size_value = aligned_size(size);
    size_t header_size = get_header_size();
    size_t total_size = aligned_size_value + alignment_;

    uint8_t* memory =
        static_cast<uint8_t*>(heap_caps_malloc(total_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (memory == nullptr)
    {
        memory = static_cast<uint8_t*>(heap_caps_malloc(total_size, MALLOC_CAP_DEFAULT));
    }

    if (memory == nullptr)
    {
        return;
    }

    uintptr_t memory_addr = reinterpret_cast<uintptr_t>(memory);
    size_t offset = (alignment_ - (memory_addr % alignment_)) % alignment_;
    uint8_t* aligned_memory = memory + offset;

    PoolBlock pool_block;
    pool_block.memory = std::unique_ptr<uint8_t[], EspHeapDeleter>(memory);
    pool_block.size = aligned_size_value;

    auto* first_block = reinterpret_cast<BlockHeader*>(aligned_memory);
    first_block->size = aligned_size_value - header_size;
    first_block->is_free = true;
    first_block->next = nullptr;
    first_block->prev = nullptr;

    pool_block.first_block = first_block;
    pool_blocks_.push_back(std::move(pool_block));

    void* first_block_ptr = reinterpret_cast<uint8_t*>(first_block) + header_size;
    auto it = free_blocks_by_size_.insert({first_block->size, first_block_ptr});
    free_blocks_iterators_[first_block_ptr] = it;
}

void* MemoryPool::allocate(size_t size)
{
    if (size == 0 || !mutex_.is_valid())
    {
        return nullptr;
    }

    MutexLockGuard lock(mutex_.get());
    if (!lock.is_locked())
    {
        return nullptr;
    }

    size_t aligned_request_size = aligned_size(size);
    size_t pool_index = 0;
    BlockHeader* block = nullptr;
    void* found_ptr = nullptr;

    auto it = free_blocks_by_size_.lower_bound(aligned_request_size);
    if (it != free_blocks_by_size_.end())
    {
        found_ptr = it->second;
        block = reinterpret_cast<BlockHeader*>(reinterpret_cast<uint8_t*>(found_ptr) -
                                               get_header_size());
        auto map_it = pointer_map_.find(found_ptr);
        if (map_it != pointer_map_.end())
        {
            pool_index = map_it->second;
        }
        free_blocks_by_size_.erase(it);
        free_blocks_iterators_.erase(found_ptr);
    }

    if (!block)
    {
        expand_pool(aligned_request_size);
        pool_index = pool_blocks_.size() - 1;
        block = pool_blocks_[pool_index].first_block;
        if (!block || !block->is_free || block->size < aligned_request_size)
        {
            return nullptr;
        }

        void* block_ptr = reinterpret_cast<uint8_t*>(block) + get_header_size();
        auto iter_it = free_blocks_iterators_.find(block_ptr);
        if (iter_it != free_blocks_iterators_.end())
        {
            free_blocks_by_size_.erase(iter_it->second);
            free_blocks_iterators_.erase(iter_it);
        }
    }

    split_block(block, aligned_request_size);
    block->is_free = false;

    auto* user_data = reinterpret_cast<uint8_t*>(block) + get_header_size();
    pointer_map_[user_data] = pool_index;

    return user_data;
}

void MemoryPool::deallocate(void* ptr)
{
    if (!ptr || !mutex_.is_valid())
    {
        return;
    }

    MutexLockGuard lock(mutex_.get());
    if (!lock.is_locked())
    {
        return;
    }

    auto pointer_iter = pointer_map_.find(ptr);
    if (pointer_iter == pointer_map_.end())
    {
        return;
    }

    size_t pool_index = pointer_iter->second;
    if (pool_index >= pool_blocks_.size())
    {
        return;
    }

    auto* block =
        reinterpret_cast<BlockHeader*>(reinterpret_cast<uint8_t*>(ptr) - get_header_size());

    block->is_free = true;
    void* block_ptr = reinterpret_cast<uint8_t*>(block) + get_header_size();
    auto it = free_blocks_by_size_.insert({block->size, block_ptr});
    free_blocks_iterators_[block_ptr] = it;

    coalesce_blocks(block);
    pointer_map_.erase(pointer_iter);
}

void MemoryPool::reset()
{
    if (!mutex_.is_valid())
    {
        return;
    }

    MutexLockGuard lock(mutex_.get());
    if (!lock.is_locked())
    {
        return;
    }

    pool_blocks_.clear();
    pointer_map_.clear();
    free_blocks_by_size_.clear();
    free_blocks_iterators_.clear();
}

MemoryPool::Stats MemoryPool::get_stats() const
{
    Stats stats{0, 0, 0, 0, 0};

    if (!mutex_.is_valid())
    {
        return stats;
    }

    MutexLockGuard lock(mutex_.get());
    if (!lock.is_locked())
    {
        return stats;
    }

    for (const auto& pool_block : pool_blocks_)
    {
        stats.total_memory += pool_block.size;

        BlockHeader* current = pool_block.first_block;
        while (current)
        {
            if (current->is_free)
            {
                stats.free_memory += current->size;
                stats.free_blocks++;
            }
            else
            {
                stats.used_memory += current->size;
                stats.allocated_blocks++;
            }
            current = current->next;
        }
    }

    return stats;
}

void MemoryPool::expand_pool(size_t required_size)
{
    auto new_size = static_cast<size_t>(
        pool_blocks_.empty() ? required_size : pool_blocks_.back().size * expansion_factor_);

    new_size = std::max(new_size, required_size + get_header_size());
    init_pool(new_size);
}

MemoryPool::BlockHeader* MemoryPool::find_free_block(size_t size)
{
    auto it = free_blocks_by_size_.lower_bound(size);
    if (it != free_blocks_by_size_.end())
    {
        void* ptr = it->second;
        auto* block =
            reinterpret_cast<BlockHeader*>(reinterpret_cast<uint8_t*>(ptr) - get_header_size());
        return block;
    }

    for (auto& pool_block : pool_blocks_)
    {
        BlockHeader* current = pool_block.first_block;
        while (current)
        {
            if (current->is_free && current->size >= size)
            {
                return current;
            }
            current = current->next;
        }
    }

    return nullptr;
}

void MemoryPool::split_block(BlockHeader* block, size_t size)
{
    size_t min_block_size = get_header_size() + alignment_;
    if (block->size >= size + min_block_size)
    {
        auto* new_block_address = reinterpret_cast<uint8_t*>(block) + get_header_size() + size;
        auto* new_block = reinterpret_cast<BlockHeader*>(new_block_address);

        new_block->size = block->size - size - get_header_size();
        new_block->is_free = true;
        new_block->next = block->next;
        new_block->prev = block;

        block->size = size;
        block->next = new_block;

        if (new_block->next)
        {
            new_block->next->prev = new_block;
        }

        void* new_block_ptr = reinterpret_cast<uint8_t*>(new_block) + get_header_size();
        auto it = free_blocks_by_size_.insert({new_block->size, new_block_ptr});
        free_blocks_iterators_[new_block_ptr] = it;
    }
}

void MemoryPool::coalesce_blocks(BlockHeader* block)
{
    BlockHeader* final_block = block;
    void* block_ptr = reinterpret_cast<uint8_t*>(block) + get_header_size();

    auto iter_it = free_blocks_iterators_.find(block_ptr);
    if (iter_it != free_blocks_iterators_.end())
    {
        free_blocks_by_size_.erase(iter_it->second);
        free_blocks_iterators_.erase(iter_it);
    }

    if (block->next && block->next->is_free)
    {
        BlockHeader* next_block = block->next;
        void* next_block_ptr = reinterpret_cast<uint8_t*>(next_block) + get_header_size();

        auto next_iter_it = free_blocks_iterators_.find(next_block_ptr);
        if (next_iter_it != free_blocks_iterators_.end())
        {
            free_blocks_by_size_.erase(next_iter_it->second);
            free_blocks_iterators_.erase(next_iter_it);
        }

        block->size += next_block->size + get_header_size();
        block->next = next_block->next;

        if (next_block->next)
        {
            next_block->next->prev = block;
        }
    }

    if (block->prev && block->prev->is_free)
    {
        BlockHeader* previous_block = block->prev;
        void* prev_block_ptr = reinterpret_cast<uint8_t*>(previous_block) + get_header_size();

        auto prev_iter_it = free_blocks_iterators_.find(prev_block_ptr);
        if (prev_iter_it != free_blocks_iterators_.end())
        {
            free_blocks_by_size_.erase(prev_iter_it->second);
            free_blocks_iterators_.erase(prev_iter_it);
        }

        previous_block->size += block->size + get_header_size();
        previous_block->next = block->next;

        if (block->next)
        {
            block->next->prev = previous_block;
        }

        final_block = previous_block;
    }

    void* final_block_ptr = reinterpret_cast<uint8_t*>(final_block) + get_header_size();
    auto it = free_blocks_by_size_.insert({final_block->size, final_block_ptr});
    free_blocks_iterators_[final_block_ptr] = it;
}

size_t MemoryPool::aligned_size(size_t size) const
{
    return (size + alignment_ - 1) & ~(alignment_ - 1);
}

size_t MemoryPool::get_header_size() const
{
    return aligned_size(sizeof(BlockHeader));
}

} // namespace app::common::memory
