#pragma once

#define BLE_TEST_DEVICE_NAME "Ebook-BLE-Test"

#ifdef __cplusplus
extern "C"
{
#endif

    /** @brief BLE 外设：GATT 服务、广播与心跳（需 NimBLE） */
    void test_bluetooth(void);

#ifdef __cplusplus
}
#endif
