#include "test_qmi8658a.hpp"

#include "qmi8658a.hpp"
#include "i2c/i2c.hpp"
#include "system/task/task.hpp"
#include "test_log.hpp"

namespace {
const char* const TAG = "test_qmi8658a";
} // namespace

extern "C" void test_qmi8658a(void)
{
    constexpr uint32_t read_retry_ms = 20;
    constexpr int read_max_attempts = 25;

    using namespace app::bsp::driver::qmi8658a;

    app::test::log_section_begin(TAG, "QMI8658A 六轴 IMU");

    app::bsp::i2c::I2C i2c;
    if (!i2c.init())
    {
        app::test::log_kv(TAG, "I2C", "init 失败");
        app::test::log_section_end(TAG);
        return;
    }
    app::test::log_kv(TAG, "I2C", "总线初始化成功");

    Qmi8658a imu;
    if (!imu.init(i2c.get_bus_handle(), QMI8658A_ADDR_LOW))
    {
        app::test::log_kv(TAG, "IMU", "init 失败（WHO_AM_I / 接线 / 地址是否为 0x6A、0x6B）");
        app::test::log_section_end(TAG);
        return;
    }
    app::test::log_kv(TAG, "IMU", "init 正常");

    SensorData data{};
    bool got = false;
    for (int n = 0; n < read_max_attempts; n++)
    {
        if (imu.read(data, READ_ALL))
        {
            got = true;
            break;
        }
        app::sys::task::TaskMgr::delay_ms(read_retry_ms);
    }

    if (!got)
    {
        app::test::log_kv(TAG, "数据", "多次读取仍无就绪数据");
        app::test::log_section_end(TAG);
        return;
    }

    app::test::log_kv_fmt(TAG, "加速度 m/s²", "X=%.3f  Y=%.3f  Z=%.3f", data.accel_x, data.accel_y,
                          data.accel_z);
    app::test::log_kv_fmt(TAG, "角速度 rad/s", "X=%.4f  Y=%.4f  Z=%.4f", data.gyro_x, data.gyro_y,
                          data.gyro_z);
    app::test::log_kv_fmt(TAG, "姿态角 °", "Roll=%.2f  Pitch=%.2f  Yaw=%.2f", data.angle_x,
                          data.angle_y, data.angle_z);

    const uint8_t motion = imu.get_motion_status();
    app::test::log_kv_fmt(TAG, "STATUS1", "0x%02X", motion);
    if ((motion & MotionStatus::ANY_MOTION) != 0U)
    {
        app::test::log_kv(TAG, "运动", "ANY_MOTION");
    }
    if ((motion & MotionStatus::NO_MOTION) != 0U)
    {
        app::test::log_kv(TAG, "运动", "NO_MOTION");
    }
    if ((motion & MotionStatus::SIGNIFICANT_MOTION) != 0U)
    {
        app::test::log_kv(TAG, "运动", "SIGNIFICANT_MOTION");
    }

    app::test::log_section_end(TAG);
}
