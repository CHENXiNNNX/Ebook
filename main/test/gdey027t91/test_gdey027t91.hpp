#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

    /** @brief 电子纸全白/全黑/条纹/局刷 */
    void test_gdey027t91_screen(void);

    /** @brief 电子纸 UI + 触摸调节背光 */
    void test_gdey027t91_touch(void);

    /** @brief FT6336U 手势轮询与串口打印 */
    void test_gdey027t91_gesture(void);

#ifdef __cplusplus
}
#endif
