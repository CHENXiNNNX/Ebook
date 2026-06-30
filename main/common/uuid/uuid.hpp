#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace app::common::uuid {

constexpr size_t UUID_BYTE_SIZE = 16;
constexpr size_t UUID_STRING_SIZE = 37;

/** @brief 128 位 UUID */
struct Uuid
{
    uint8_t data[UUID_BYTE_SIZE];

    bool operator==(const Uuid& other) const
    {
        return memcmp(data, other.data, UUID_BYTE_SIZE) == 0;
    }

    bool operator!=(const Uuid& other) const
    {
        return !(*this == other);
    }
};

Uuid generate();
bool to_string(const Uuid& uuid, char* buffer, size_t buffer_size);
bool from_string(const char* str, Uuid& uuid);
bool is_empty(const Uuid& uuid);

} // namespace app::common::uuid
