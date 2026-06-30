#include "uuid.hpp"

#include <cstdio>
#include <cstring>

#include "esp_random.h"

namespace app::common::uuid {

Uuid generate()
{
    Uuid uuid;

    for (size_t i = 0; i < UUID_BYTE_SIZE; i++)
    {
        uuid.data[i] = static_cast<uint8_t>(esp_random() & 0xFF);
    }

    uuid.data[6] = (uuid.data[6] & 0x0F) | 0x40; // 版本号 4
    uuid.data[8] = (uuid.data[8] & 0x3F) | 0x80; // 变体标识

    return uuid;
}

bool to_string(const Uuid& uuid, char* buffer, size_t buffer_size)
{
    if (buffer == nullptr || buffer_size < UUID_STRING_SIZE)
    {
        return false;
    }

    constexpr size_t UUID_STRING_LENGTH = 36; // 不包括 null 终止符
    int result = snprintf(
        buffer, buffer_size, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        uuid.data[0], uuid.data[1], uuid.data[2], uuid.data[3], uuid.data[4], uuid.data[5],
        uuid.data[6], uuid.data[7], uuid.data[8], uuid.data[9], uuid.data[10], uuid.data[11],
        uuid.data[12], uuid.data[13], uuid.data[14], uuid.data[15]);

    return result == UUID_STRING_LENGTH;
}

bool from_string(const char* str, Uuid& uuid)
{
    if (str == nullptr)
    {
        return false;
    }

    unsigned int bytes[UUID_BYTE_SIZE];
    int result =
        sscanf(str, "%2x%2x%2x%2x-%2x%2x-%2x%2x-%2x%2x-%2x%2x%2x%2x%2x%2x", &bytes[0], &bytes[1],
               &bytes[2], &bytes[3], &bytes[4], &bytes[5], &bytes[6], &bytes[7], &bytes[8],
               &bytes[9], &bytes[10], &bytes[11], &bytes[12], &bytes[13], &bytes[14], &bytes[15]);

    if (result != static_cast<int>(UUID_BYTE_SIZE))
    {
        return false;
    }

    for (size_t i = 0; i < UUID_BYTE_SIZE; i++)
    {
        uuid.data[i] = static_cast<uint8_t>(bytes[i]);
    }

    return true;
}

bool is_empty(const Uuid& uuid)
{
    if (reinterpret_cast<uintptr_t>(uuid.data) % 8 == 0)
    {
        constexpr uint64_t zero64 = 0;
        const uint64_t* data64 = reinterpret_cast<const uint64_t*>(uuid.data);
        return data64[0] == zero64 && data64[1] == zero64;
    }
    else
    {
        for (size_t i = 0; i < UUID_BYTE_SIZE; i++)
        {
            if (uuid.data[i] != 0)
            {
                return false;
            }
        }
        return true;
    }
}

} // namespace app::common::uuid
