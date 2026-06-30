#pragma once

#define WIFI_TEST_SSID "yf"
#define WIFI_TEST_PASSWORD "yf123456"

#ifdef __cplusplus
extern "C"
{
#endif

    /** @brief WiFiMgr 扫描、凭据、连接全流程 */
    void test_wifi(void);

#ifdef __cplusplus
}
#endif
