#include "router/refresh_intent.hpp"

namespace app::ebook::router {

const char* waveform_name(Waveform w)
{
    switch (w)
    {
        case Waveform::Partial: return "Partial";
        case Waveform::Fast:    return "Fast";
        case Waveform::Full:    return "Full";
    }
    return "?";
}

} // namespace app::ebook::router
