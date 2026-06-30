#pragma once

#include <cstdint>

namespace app::ebook::composer {

/** @brief 按 Router 栈合成 back 帧（Shell → App → Overlay） */
class Composer
{
  public:
    static Composer& instance();

    void paint(uint8_t* back_fb);

  private:
    Composer() = default;
};

} // namespace app::ebook::composer
