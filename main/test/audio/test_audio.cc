#include "test_audio.hpp"

#include <cmath>
#include <cstdint>

#include "es8311_codec.hpp"
#include "i2c/i2c.hpp"
#include "test_log.hpp"

namespace {

const char* const TAG = "test_audio";

using app::bsp::driver::audio::es8311::Config;
using app::bsp::driver::audio::es8311::Es8311Codec;
using app::bsp::i2c::I2C;

constexpr float kPi = 3.14159265358979323846F;
constexpr uint32_t kSampleRate = 16000;
constexpr uint16_t kFrameSamples = 160;
constexpr float kToneHz = 1000.0F;
constexpr float kSineAmplitude = 16000.0F;
constexpr uint32_t kPlayDurationS = 5;
constexpr uint32_t kLoopDurationS = 10;
constexpr uint8_t kOutputVolume = 100;
constexpr uint8_t kInputGainDb = 10;

/** I2C 与编解码器同生命周期 */
struct AudioHarness
{
    I2C i2c;
    Es8311Codec codec;

    bool setup()
    {
        if (!i2c.init())
        {
            app::test::log_kv(TAG, "I2C", "init 失败");
            return false;
        }

        Config cfg;
        cfg.sample_rate = kSampleRate;

        if (!codec.init(i2c.get_bus_handle(), &cfg))
        {
            app::test::log_kv(TAG, "ES8311", "init 失败");
            return false;
        }
        return true;
    }

    void shutdown_paths()
    {
        codec.enable_input(false);
        codec.enable_output(false);
    }
};

uint32_t frames_for_seconds(uint32_t seconds)
{
    return (kSampleRate * seconds) / kFrameSamples;
}

void generate_sine_frame(int16_t* buf, uint16_t len, uint32_t& phase)
{
    for (uint16_t i = 0; i < len; i++)
    {
        const float t = static_cast<float>(phase) / static_cast<float>(kSampleRate);
        buf[i] = static_cast<int16_t>(kSineAmplitude * sinf(2.0F * kPi * kToneHz * t));
        phase++;
        if (phase >= kSampleRate)
        {
            phase -= kSampleRate;
        }
    }
}

bool run_play(Es8311Codec& codec)
{
    codec.set_output_volume(kOutputVolume);
    codec.enable_output(true);

    app::test::log_kv_fmt(TAG, "参数", "%u Hz  %.0f Hz  %u 秒  帧=%u  vol=%u%%",
                          static_cast<unsigned>(kSampleRate), static_cast<double>(kToneHz),
                          static_cast<unsigned>(kPlayDurationS),
                          static_cast<unsigned>(kFrameSamples), codec.get_output_volume());

    const uint32_t total = frames_for_seconds(kPlayDurationS);
    int16_t frame[kFrameSamples];
    uint32_t phase = 0;
    bool ok = true;

    for (uint32_t n = 0; n < total; n++)
    {
        generate_sine_frame(frame, kFrameSamples, phase);
        const int ret = codec.write(frame, kFrameSamples);
        if (ret != kFrameSamples)
        {
            app::test::log_kv_fmt(TAG, "异常", "write 返回 %d (期望 %u)", ret,
                                  static_cast<unsigned>(kFrameSamples));
            ok = false;
            break;
        }
    }

    codec.enable_output(false);
    return ok;
}

bool run_loop(Es8311Codec& codec)
{
    codec.set_output_volume(kOutputVolume);
    codec.set_input_gain(kInputGainDb);
    codec.enable_input(true);
    codec.enable_output(true);

    app::test::log_kv_fmt(TAG, "参数", "%u Hz  %u 秒  帧=%u  out=%u%%  in=%u dB",
                          static_cast<unsigned>(kSampleRate), static_cast<unsigned>(kLoopDurationS),
                          static_cast<unsigned>(kFrameSamples), codec.get_output_volume(),
                          codec.get_input_gain());
    app::test::log_kv(TAG, "提示", "麦克风勿贴喇叭，避免啸叫");

    const uint32_t total = frames_for_seconds(kLoopDurationS);
    int16_t frame[kFrameSamples];
    bool ok = true;
    uint32_t fail_read = 0;
    uint32_t fail_write = 0;

    for (uint32_t n = 0; n < total; n++)
    {
        const int rd = codec.read(frame, kFrameSamples);
        if (rd != kFrameSamples)
        {
            fail_read++;
            ok = false;
            continue;
        }

        const int wr = codec.write(frame, kFrameSamples);
        if (wr != kFrameSamples)
        {
            fail_write++;
            ok = false;
        }
    }

    if (fail_read > 0)
    {
        app::test::log_kv_fmt(TAG, "统计", "read 失败 %u 次", static_cast<unsigned>(fail_read));
    }
    if (fail_write > 0)
    {
        app::test::log_kv_fmt(TAG, "统计", "write 失败 %u 次", static_cast<unsigned>(fail_write));
    }

    codec.enable_input(false);
    codec.enable_output(false);
    return ok;
}

} // namespace

extern "C" void test_audio_play(void)
{
    app::test::log_section_begin(TAG, "音频播放测试");

    AudioHarness harness;
    if (!harness.setup())
    {
        app::test::log_check(TAG, "初始化", false);
        app::test::log_section_end(TAG);
        return;
    }
    app::test::log_check(TAG, "初始化", true);

    const bool ok = run_play(harness.codec);

    app::test::log_check(TAG, "正弦波播放", ok);
    app::test::log_check(TAG, "音频播放测试汇总", ok);
    app::test::log_section_end(TAG);
}

extern "C" void test_audio_loop(void)
{
    app::test::log_section_begin(TAG, "音频回环测试");

    AudioHarness harness;
    if (!harness.setup())
    {
        app::test::log_check(TAG, "初始化", false);
        app::test::log_section_end(TAG);
        return;
    }
    app::test::log_check(TAG, "初始化", true);

    const bool ok = run_loop(harness.codec);
    harness.shutdown_paths();

    app::test::log_check(TAG, "实时回环", ok);
    app::test::log_check(TAG, "音频回环测试汇总", ok);
    app::test::log_section_end(TAG);
}
