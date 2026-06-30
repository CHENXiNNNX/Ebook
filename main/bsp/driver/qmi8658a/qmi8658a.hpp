#pragma once

#include <cstdint>
#include <functional>
#include <memory>

#include <driver/i2c_master.h>

#include "system/task/task.hpp"

namespace app::bsp::driver::qmi8658a {

constexpr uint8_t QMI8658A_ADDR_LOW = 0x6A;
constexpr uint8_t QMI8658A_ADDR_HIGH = 0x6B;

enum ReadOption : uint8_t
{
    READ_SENSOR = 0x01,
    READ_ATTITUDE = 0x02,
    READ_ALL = READ_SENSOR | READ_ATTITUDE,
};

enum MotionStatus : uint8_t
{
    ANY_MOTION = 0x20,
    NO_MOTION = 0x40,
    SIGNIFICANT_MOTION = 0x80,
};

struct SensorData
{
    int16_t acc_x_raw;
    int16_t acc_y_raw;
    int16_t acc_z_raw;
    int16_t gyr_x_raw;
    int16_t gyr_y_raw;
    int16_t gyr_z_raw;

    float accel_x;
    float accel_y;
    float accel_z;
    float gyro_x;
    float gyro_y;
    float gyro_z;

    float angle_x;
    float angle_y;
    float angle_z;
};

struct AngleData
{
    float roll;
    float pitch;
    float yaw;
};

/** QMI8658A 六轴 IMU + 可选后台运动检测任务 */
class Qmi8658a
{
  public:
    Qmi8658a();
    ~Qmi8658a();

    bool init(i2c_master_bus_handle_t bus_handle, uint8_t i2c_addr = QMI8658A_ADDR_LOW);
    void deinit();

    bool read(SensorData& data, uint8_t options = READ_SENSOR);
    uint8_t get_motion_status();
    void close();

    bool is_init() const { return initialized_; }

    bool start_data_collection(uint32_t interval_ms = 100);
    bool stop_data_collection();

    using MotionStatusCallback = std::function<void(int motion_status)>;

    void set_motion_status_callback(MotionStatusCallback callback)
    {
        motion_status_callback_ = callback;
    }

    int get_current_motion_status() const { return current_motion_status_; }

    bool calibrate();
    void reset_calibration();
    bool is_calibrated() const { return calibrated_; }

    bool get_current_angle(AngleData& angle);
    bool get_relative_angle(AngleData& angle);

  private:
    enum Reg : uint8_t
    {
        WHO_AM_I = 0x00,
        REVISION_ID = 0x01,
        CTRL1 = 0x02,
        CTRL2 = 0x03,
        CTRL3 = 0x04,
        CTRL7 = 0x08,
        CTRL8 = 0x09,
        CTRL9 = 0x0A,
        CATL1_L = 0x0B,
        CATL1_H = 0x0C,
        CATL2_L = 0x0D,
        CATL2_H = 0x0E,
        CATL3_L = 0x0F,
        CATL3_H = 0x10,
        CATL4_L = 0x11,
        CATL4_H = 0x12,
        STATUS0 = 0x2E,
        STATUS1 = 0x2F,
        AX_L = 0x35,
        RESET = 0x60,
    };

    bool write_register(uint8_t reg, uint8_t value);
    bool read_register(uint8_t reg, uint8_t* buffer, size_t length);
    void calculate_attitude(SensorData& data);
    void data_collection_task_function(void* param);

    i2c_master_bus_handle_t bus_handle_ = nullptr;
    i2c_master_dev_handle_t dev_handle_ = nullptr;
    bool initialized_ = false;
    uint8_t i2c_addr_ = QMI8658A_ADDR_LOW;

    bool calibrated_ = false;
    AngleData reference_angle_{};

    static constexpr float ACCEL_SCALE = 9.807f / 8192.0f;
    static constexpr float GYRO_SCALE = 0.0174533f / 64.0f;

    std::unique_ptr<app::sys::task::Task> data_collection_task_;
    uint32_t collection_interval_ms_ = 100;
    bool collection_running_ = false;

    MotionStatusCallback motion_status_callback_ = nullptr;
    int current_motion_status_ = -1;

    static constexpr float ACCEL_CHANGE_THRESHOLD = 2.0f;

    float last_accel_x_ = 0.0f;
    float last_accel_y_ = 0.0f;
    float last_accel_z_ = 0.0f;
    bool has_last_accel_ = false;
};

} // namespace app::bsp::driver::qmi8658a
