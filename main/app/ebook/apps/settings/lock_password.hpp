#pragma once

#include <cstddef>
#include <cstdint>

namespace app::ebook::apps::settings {

/** @brief 锁屏 PIN 仓库（NVS: sec.lock.on / sec.lock.h，SHA256 存哈希） */
class LockPassword
{
  public:
    static constexpr uint8_t kPinLen = 4;

    static LockPassword& get_instance();

    void load();

    bool enabled() const { return enabled_; }

    static bool valid_pin(const char* pin);

    bool verify(const char* pin) const;
    bool set_new(const char* pin);
    void clear();

  private:
    LockPassword() = default;

    bool hash_pin(const char* pin, char* hex_out, size_t hex_cap) const;
    bool matches_hash(const char* pin) const;

    bool enabled_{false};
    char hash_hex_[65]{};
};

} // namespace app::ebook::apps::settings
