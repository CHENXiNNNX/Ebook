#pragma once

#include <cstdint>

namespace app::ebook::gfx::icon {

inline constexpr uint32_t kFaReply         = 0xF112;
inline constexpr uint32_t kFaChevronLeft   = 0xF053;
inline constexpr uint32_t kFaChevronRight  = 0xF054;
inline constexpr uint32_t kFaChevronUp     = 0xF077;
inline constexpr uint32_t kFaChevronDown   = 0xF078;
inline constexpr uint32_t kFaCheck         = 0xF00C;
inline constexpr uint32_t kFaTimes         = 0xF00D;
inline constexpr uint32_t kFaPlus          = 0xF067;
inline constexpr uint32_t kFaMinus         = 0xF068;
inline constexpr uint32_t kFaSearch        = 0xF002;
inline constexpr uint32_t kFaSync          = 0xF021;
inline constexpr uint32_t kFaBackspace     = 0xF55A;
inline constexpr uint32_t kFaLock          = 0xF023;
inline constexpr uint32_t kFaUnlock        = 0xF09C;
inline constexpr uint32_t kFaEllipsisH     = 0xF141;
inline constexpr uint32_t kFaToggleOff     = 0xF204;
inline constexpr uint32_t kFaToggleOn      = 0xF205;
inline constexpr uint32_t kFaWifi          = 0xF1EB;
inline constexpr uint32_t kFaMoon          = 0xF186;
inline constexpr uint32_t kFaSun           = 0xF185;
inline constexpr uint32_t kFaVolumeUp      = 0xF028;
inline constexpr uint32_t kFaVolumeMute    = 0xF6A9;
inline constexpr uint32_t kFaPlug          = 0xF1E6;
inline constexpr uint32_t kFaBatteryFull    = 0xF240;
inline constexpr uint32_t kFaBatteryQuarter = 0xF243;
inline constexpr uint32_t kFaBookReader    = 0xF5DA;
inline constexpr uint32_t kFaKeyboard      = 0xF11C;
inline constexpr uint32_t kFaStickyNote    = 0xF249;
inline constexpr uint32_t kFaImages        = 0xF302;
inline constexpr uint32_t kFaPaintBrush    = 0xF1FC;
inline constexpr uint32_t kFaCloudSun      = 0xF6C4;
inline constexpr uint32_t kFaMusic         = 0xF001;
inline constexpr uint32_t kFaPlay          = 0xF04B;
inline constexpr uint32_t kFaPause         = 0xF04C;
inline constexpr uint32_t kFaStepBackward  = 0xF048;
inline constexpr uint32_t kFaStepForward   = 0xF051;
inline constexpr uint32_t kFaClock         = 0xF017;
inline constexpr uint32_t kFaCalendarAlt   = 0xF073;
inline constexpr uint32_t kFaFolder        = 0xF07B;
inline constexpr uint32_t kFaFolderOpen    = 0xF07C;
inline constexpr uint32_t kFaFile          = 0xF15B;
inline constexpr uint32_t kFaSdCard        = 0xF7C2;
inline constexpr uint32_t kFaExchangeAlt   = 0xF362;
inline constexpr uint32_t kFaCog           = 0xF013;
inline constexpr uint32_t kFaInfoCircle    = 0xF05A;
inline constexpr uint32_t kFaHotspot       = 0xF519;
inline constexpr uint32_t kFaHdd           = 0xF0A0;
inline constexpr uint32_t kFaShield        = 0xF132;
inline constexpr uint32_t kFaDownload      = 0xF019;
inline constexpr uint32_t kFaTv            = 0xF26C;
inline constexpr uint32_t kEbBluetooth     = 0xE001;
inline constexpr uint32_t kEbWoodenStick   = 0xE002;
inline constexpr uint32_t kEbWoodenFish    = 0xE003;

/** 设置项开关图标 */
inline uint32_t toggle(bool on)
{
    return on ? kFaToggleOn : kFaToggleOff;
}

} // namespace app::ebook::gfx::icon
