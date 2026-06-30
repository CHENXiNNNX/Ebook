#include "apps/music/music_store.hpp"

#include "data/persist.hpp"

namespace app::ebook::apps::music {

namespace {
constexpr const char* kKLastIndex = "music.idx";
}

MusicStore& MusicStore::get_instance()
{
    static MusicStore s;
    return s;
}

void MusicStore::load()
{
    uint8_t idx = 0;
    if (data::Persist::get_u8(kKLastIndex, idx))
        last_index_ = idx;
}

void MusicStore::save()
{
    (void)data::Persist::set_u8(kKLastIndex, last_index_);
    data::Persist::commit();
}

void MusicStore::set_last_index(uint8_t idx)
{
    if (idx == last_index_) return;
    last_index_ = idx;
    save();
}

} // namespace app::ebook::apps::music
