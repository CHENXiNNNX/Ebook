#pragma once

#include <cstdint>

#include "input/input_event.hpp"
#include "input/physical_types.hpp"

namespace app::ebook::ui {

enum class UiEventKind : uint8_t
{
    None = 0,
    Input,
    PhysicalInput,
    TickClock,
    TickBattery,
    WifiState,
    WifiScanDone,
    NtpSyncDone,
    SystemHint,
};

enum class SystemHintKind : uint8_t
{
    None = 0,
    ReaderIndexProgress,
    ReaderIndexDone,
    ClockAlarm,
    WeatherFetchDone,
    MusicScanDone,
    GalleryScanDone,
    GalleryDecodeDone,
    FilesReloadDone,
    MusicStateChanged,
    MusicProgress,
    ToastExpire,
    WoodenFishAnimDone,
    AutoLock,
};

struct UiEvent
{
    UiEventKind kind{UiEventKind::None};

    union Payload
    {
        Payload() : raw{} {}

        ::app::ebook::input::Event input;

        struct
        {
            uint8_t key;
            uint8_t action;
        } physical;

        struct
        {
            uint8_t  pct;
            uint32_t mv;
        } battery;

        struct
        {
            uint8_t hour;
            uint8_t minute;
            uint8_t second;
        } clock;

        struct
        {
            uint8_t state;
            uint8_t fail;
        } wifi;

        struct
        {
            SystemHintKind hint;
            uint32_t       value;
        } system;

        uint8_t raw[16];
    } payload;

    UiEvent() = default;
};

static_assert(sizeof(UiEvent) <= 32, "UiEvent must stay compact for fifo");

} // namespace app::ebook::ui
