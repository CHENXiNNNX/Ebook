#include "apps/settings/lock_password.hpp"

#include <cstdio>
#include <cstring>

#include <psa/crypto.h>

#include "data/persist.hpp"

namespace app::ebook::apps::settings {

namespace {

constexpr const char* kKOn   = "sec.lock.on";
constexpr const char* kKHash = "sec.lock.h";

bool is_digit_pin(const char* pin)
{
    if (pin == nullptr)
        return false;
    for (uint8_t i = 0; i < LockPassword::kPinLen; ++i)
    {
        const char c = pin[i];
        if (c < '0' || c > '9')
            return false;
    }
    return pin[LockPassword::kPinLen] == '\0';
}

void bytes_to_hex(const unsigned char* in, size_t n, char* out, size_t out_cap)
{
    if (out == nullptr || out_cap < n * 2U + 1U)
        return;
    for (size_t i = 0; i < n; ++i)
        (void)std::snprintf(out + i * 2U, out_cap - i * 2U, "%02x",
                            static_cast<unsigned>(in[i]));
    out[n * 2U] = '\0';
}

} // namespace

LockPassword& LockPassword::get_instance()
{
    static LockPassword s;
    return s;
}

void LockPassword::load()
{
    enabled_  = false;
    hash_hex_[0] = '\0';

    bool on = false;
    if (data::Persist::get_bool(kKOn, on))
        enabled_ = on;

    char buf[sizeof(hash_hex_)] = {};
    if (data::Persist::get_str(kKHash, buf, sizeof(buf)) && buf[0] != '\0')
    {
        std::strncpy(hash_hex_, buf, sizeof(hash_hex_) - 1);
        hash_hex_[sizeof(hash_hex_) - 1] = '\0';
    }

    if (enabled_ && hash_hex_[0] == '\0')
        enabled_ = false;
}

bool LockPassword::valid_pin(const char* pin)
{
    return is_digit_pin(pin);
}

bool LockPassword::hash_pin(const char* pin, char* hex_out, size_t hex_cap) const
{
    if (pin == nullptr || hex_out == nullptr || hex_cap < 65U)
        return false;

    unsigned char digest[32]{};
    size_t        hash_len = 0;
    const psa_status_t st  = psa_hash_compute(
        PSA_ALG_SHA_256, reinterpret_cast<const uint8_t*>(pin), std::strlen(pin),
        digest, sizeof(digest), &hash_len);
    if (st != PSA_SUCCESS || hash_len != 32U)
        return false;

    bytes_to_hex(digest, 32, hex_out, hex_cap);
    return true;
}

bool LockPassword::matches_hash(const char* pin) const
{
    if (!valid_pin(pin) || hash_hex_[0] == '\0')
        return false;

    char hex[65]{};
    if (!hash_pin(pin, hex, sizeof(hex)))
        return false;
    return std::strcmp(hex, hash_hex_) == 0;
}

bool LockPassword::verify(const char* pin) const
{
    if (!enabled_)
        return true;
    return matches_hash(pin);
}

bool LockPassword::set_new(const char* pin)
{
    if (!valid_pin(pin))
        return false;

    char hex[65]{};
    if (!hash_pin(pin, hex, sizeof(hex)))
        return false;

    std::strncpy(hash_hex_, hex, sizeof(hash_hex_) - 1);
    hash_hex_[sizeof(hash_hex_) - 1] = '\0';
    enabled_ = true;

    (void)data::Persist::set_bool(kKOn, true);
    (void)data::Persist::set_str(kKHash, hash_hex_);
    data::Persist::commit();
    return true;
}

void LockPassword::clear()
{
    enabled_     = false;
    hash_hex_[0] = '\0';
    (void)data::Persist::set_bool(kKOn, false);
    (void)data::Persist::set_str(kKHash, "");
    data::Persist::commit();
}

} // namespace app::ebook::apps::settings
