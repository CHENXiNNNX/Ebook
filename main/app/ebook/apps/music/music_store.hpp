#pragma once

#include <cstdint>

namespace app::ebook::apps::music {

/** 记忆「上次播放的曲目索引」 */
class MusicStore
{
  public:
    static MusicStore& get_instance();

    void load();
    void save();

    uint8_t last_index() const { return last_index_; }
    void    set_last_index(uint8_t idx);

  private:
    MusicStore() = default;

    uint8_t last_index_{0};
};

} // namespace app::ebook::apps::music
