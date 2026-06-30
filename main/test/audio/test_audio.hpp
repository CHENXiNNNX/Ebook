#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

    /** @brief ES8311 正弦波播放（1 kHz，5 秒） */
    void test_audio_play(void);

    /** @brief ES8311 麦克风回环（10 秒） */
    void test_audio_loop(void);

#ifdef __cplusplus
}
#endif
