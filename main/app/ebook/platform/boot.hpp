#pragma once

#include "core/result.hpp"

namespace app::ebook::platform {

/** @brief 启动链：system → persist → display → touch → data → battery → tasks */
core::Status boot();

} // namespace app::ebook::platform
