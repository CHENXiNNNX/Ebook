#pragma once

#include <cstdint>

namespace app::ebook::apps {

enum class AppId : uint8_t
{
    None = 0,
    Reader,
    Notepad,
    Gallery,
    Drawing,
    WoodenFish,
    Music,
    Weather,
    Clock,
    Calendar,
    Files,
    Update,
    Settings,
    Count
};

constexpr const char* app_id_name(AppId id)
{
    switch (id)
    {
        case AppId::Reader:     return "reader";
        case AppId::Notepad:    return "notepad";
        case AppId::Gallery:    return "gallery";
        case AppId::Drawing:    return "drawing";
        case AppId::WoodenFish: return "wooden_fish";
        case AppId::Music:      return "music";
        case AppId::Weather:    return "weather";
        case AppId::Clock:      return "clock";
        case AppId::Calendar:   return "calendar";
        case AppId::Files:      return "files";
        case AppId::Update:     return "update";
        case AppId::Settings:   return "settings";
        case AppId::None:
        case AppId::Count:      break;
    }
    return "?";
}

} // namespace app::ebook::apps
