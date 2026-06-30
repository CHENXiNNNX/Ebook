#pragma once

#include <cstdint>

namespace app::ebook::core {

/** @brief ebook 返回码（与 esp_err_t 不互通，进 app 层前须显式映射） */
enum class Status : uint8_t
{
    Ok = 0,
    InvalidArg,
    NotInit,
    AlreadyInit,
    Busy,
    Timeout,
    OutOfResource,
    IoError,
    NotSupported,
    NotFound,
    Cancelled,
    Internal,
};

constexpr bool ok(Status s) { return s == Status::Ok; }
constexpr bool fail(Status s) { return s != Status::Ok; }

constexpr const char* to_str(Status s)
{
    switch (s)
    {
        case Status::Ok:            return "OK";
        case Status::InvalidArg:    return "INVALID_ARG";
        case Status::NotInit:       return "NOT_INIT";
        case Status::AlreadyInit:   return "ALREADY_INIT";
        case Status::Busy:          return "BUSY";
        case Status::Timeout:       return "TIMEOUT";
        case Status::OutOfResource: return "OUT_OF_RESOURCE";
        case Status::IoError:       return "IO_ERROR";
        case Status::NotSupported:  return "NOT_SUPPORTED";
        case Status::NotFound:      return "NOT_FOUND";
        case Status::Cancelled:     return "CANCELLED";
        case Status::Internal:      return "INTERNAL";
    }
    return "?";
}

} // namespace app::ebook::core
