#pragma once

#include <cstdint>

namespace app::bsp::driver::gdey027t91::ssd1680 {

struct Cmd
{
    static constexpr uint8_t kDriverOutputControl = 0x01;
    static constexpr uint8_t kDataEntryMode = 0x11;
    static constexpr uint8_t kSwReset = 0x12;
    static constexpr uint8_t kDeepSleep = 0x10;
    static constexpr uint8_t kTempSensor = 0x18;
    static constexpr uint8_t kDisplayUpdateCtrl1 = 0x22;
    static constexpr uint8_t kDisplayUpdateCtrl2 = 0x20;
    static constexpr uint8_t kBorderWaveform = 0x3C;
    static constexpr uint8_t kSetRamX = 0x44;
    static constexpr uint8_t kSetRamY = 0x45;
    static constexpr uint8_t kSetRamXCounter = 0x4E;
    static constexpr uint8_t kSetRamYCounter = 0x4F;
    static constexpr uint8_t kWriteRamBw = 0x24;
    static constexpr uint8_t kWriteRamBwPrev = 0x26;
};

struct Seq
{
    static constexpr uint8_t kFull = 0xF7;
    static constexpr uint8_t kFast = 0xD7;
    static constexpr uint8_t kPartial = 0xFC;
    static constexpr uint8_t kPowerOn = 0xE0;
    static constexpr uint8_t kPowerOff = 0x83;
};

struct Param
{
    static constexpr uint8_t kDataEntryNormal = 0x03;
    static constexpr uint8_t kBorderNormal = 0x05;
    static constexpr uint8_t kTempInternal = 0x80;
    static constexpr uint8_t kDeepSleepMode = 0x01;
    static constexpr uint8_t kBoosterTiming = 0x64;
};

} // namespace app::bsp::driver::gdey027t91::ssd1680
