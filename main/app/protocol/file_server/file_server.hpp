#pragma once

#include "esp_err.h"

namespace app::protocol::file_server {

/** @brief 热点局域网文件传输（HTTP 80 + DNS Captive Portal，操作 /int） */
esp_err_t start();

void stop();

bool is_running();

} // namespace app::protocol::file_server
