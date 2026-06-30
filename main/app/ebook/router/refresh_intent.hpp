#pragma once

#include <cstdint>

namespace app::ebook::router {

/** 刷新波形：Partial / Fast / Full（均全屏合成，无区域裁剪） */
enum class Waveform : uint8_t
{
    Partial,
    Fast,
    Full,
};

struct RefreshIntent
{
    Waveform waveform{Waveform::Partial};
};

constexpr RefreshIntent intent_partial_full()
{
    return RefreshIntent{Waveform::Partial};
}

constexpr RefreshIntent intent_fast_full()
{
    return RefreshIntent{Waveform::Fast};
}

constexpr RefreshIntent intent_full_full()
{
    return RefreshIntent{Waveform::Full};
}

const char* waveform_name(Waveform w);

} // namespace app::ebook::router
