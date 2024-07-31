/*
 * This file is part of the [rfmarkit-esp-node].
 *
 * Original code from esp32_BNO08x (https://github.com/myles-parfeniuk/esp32_BNO08x)
 * by Myles Parfeniuk, licensed under the MIT License.
 *
 * Modifications by [davidliyutong], [2024].
 */

#ifndef _BNO08X_DRIVER_H
#define _BNO08X_DRIVER_H

//esp-idf includes
#include <driver/gpio.h>
#include <driver/spi_common.h>
#include <driver/spi_master.h>
#include <esp_log.h>
#include <esp_rom_gpio.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <freertos/queue.h>
#include <rom/ets_sys.h>

//std library includes
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/// @brief SHTP protocol channels
enum channels_t
{
    CHANNEL_COMMAND,
    CHANNEL_EXECUTABLE,
    CHANNEL_CONTROL,
    CHANNEL_REPORTS,
    CHANNEL_WAKE_REPORTS,
    CHANNEL_GYRO
};

/// @brief Sensor accuracy returned during sensor calibration
typedef enum
{
    IMU_ACCURACY_LOW = 1,
    IMU_ACCURACY_MED,
    IMU_ACCURACY_HIGH
} IMUAccuracy;

/// @brief SPI event group bits (indicates events in the sending and receiving process)
typedef enum
{
    EVT_GRP_SPI_RX_DONE_BIT = (1 << 0),
    EVT_GRP_SPI_RX_VALID_PACKET_BIT = (1 << 1),
    EVT_GRP_SPI_RX_INVALID_PACKET_BIT = (1 << 2),
    EVT_GRP_SPI_TX_DONE_BIT = (1 << 3)
} evt_grp_spi_bits_t;

/// @brief Report enable event group bits (indicates which reports are active)
typedef enum
{
    // evt_grp_report_en bits
    EVT_GRP_RPT_ROTATION_VECTOR_BIT = (1 << 0),      ///< When set, rotation vector reports are active.
    EVT_GRP_RPT_GAME_ROTATION_VECTOR_BIT = (1 << 1), ///< When set, game rotation vector reports are active.
    EVT_GRP_RPT_ARVR_S_ROTATION_VECTOR_BIT =
    (1 << 2), ///< When set, ARVR stabilized rotation vector reports are active.
    EVT_GRP_RPT_ARVR_S_GAME_ROTATION_VECTOR_BIT =
    (1 << 3), ///< When set, ARVR stabilized game rotation vector reports are active.
    EVT_GRP_RPT_GYRO_ROTATION_VECTOR_BIT =
    (1 << 4),                                     ///< When set, gyro integrator rotation vector reports are active.
    EVT_GRP_RPT_ACCELEROMETER_BIT = (1 << 5),         ///< When set, accelerometer reports are active.
    EVT_GRP_RPT_LINEAR_ACCELEROMETER_BIT = (1 << 6),  ///< When set, linear accelerometer reports are active.
    EVT_GRP_RPT_GRAVITY_BIT = (1 << 7),               ///< When set, gravity reports are active.
    EVT_GRP_RPT_GYRO_BIT = (1 << 8),                  ///< When set, gyro reports are active.
    EVT_GRP_RPT_GYRO_UNCALIBRATED_BIT = (1 << 9),     ///< When set, uncalibrated gyro reports are active.
    EVT_GRP_RPT_MAGNETOMETER_BIT = (1 << 10),         ///< When set, magnetometer reports are active.
    EVT_GRP_RPT_TAP_DETECTOR_BIT = (1 << 11),         ///< When set, tap detector reports are active.
    EVT_GRP_RPT_STEP_COUNTER_BIT = (1 << 12),         ///< When set, step counter reports are active.
    EVT_GRP_RPT_STABILITY_CLASSIFIER_BIT = (1 << 13), ///< When set, stability classifier reports are active.
    EVT_GRP_RPT_ACTIVITY_CLASSIFIER_BIT = (1 << 14),  ///< When set, activity classifier reports are active.
    EVT_GRP_RPT_RAW_ACCELEROMETER_BIT = (1 << 15),    ///< When set, raw accelerometer reports are active.
    EVT_GRP_RPT_RAW_GYRO_BIT = (1 << 16),             ///< When set, raw gyro reports are active.
    EVT_GRP_RPT_RAW_MAGNETOMETER_BIT = (1 << 17),     ///< When set, raw magnetometer reports are active.

    EVT_GRP_RPT_ALL_BITS =
    EVT_GRP_RPT_ROTATION_VECTOR_BIT | EVT_GRP_RPT_GAME_ROTATION_VECTOR_BIT | EVT_GRP_RPT_ARVR_S_ROTATION_VECTOR_BIT |
    EVT_GRP_RPT_ARVR_S_GAME_ROTATION_VECTOR_BIT | EVT_GRP_RPT_GYRO_ROTATION_VECTOR_BIT | EVT_GRP_RPT_ACCELEROMETER_BIT |
    EVT_GRP_RPT_LINEAR_ACCELEROMETER_BIT | EVT_GRP_RPT_GRAVITY_BIT | EVT_GRP_RPT_GYRO_BIT | EVT_GRP_RPT_GYRO_UNCALIBRATED_BIT |
    EVT_GRP_RPT_MAGNETOMETER_BIT | EVT_GRP_RPT_TAP_DETECTOR_BIT | EVT_GRP_RPT_STEP_COUNTER_BIT | EVT_GRP_RPT_STABILITY_CLASSIFIER_BIT |
    EVT_GRP_RPT_ACTIVITY_CLASSIFIER_BIT | EVT_GRP_RPT_RAW_ACCELEROMETER_BIT | EVT_GRP_RPT_RAW_GYRO_BIT | EVT_GRP_RPT_RAW_MAGNETOMETER_BIT

} evt_grp_report_en_bits_t;

/// @brief IMU configuration settings passed into constructor
typedef struct
{
    spi_host_device_t spi_peripheral;
    gpio_num_t io_mosi;
    gpio_num_t io_miso;
    gpio_num_t io_sclk;
    gpio_num_t io_cs;
    gpio_num_t io_int;
    gpio_num_t io_rst;
    gpio_num_t io_wake;
    int32_t sclk_speed;
#if ESP_IDF_VERSION_MAJOR == 5
    uint8_t cpu_spi_intr_affinity;
#endif
} BNO08x_config_t;

/// @brief Holds data that is sent over spi.
typedef struct bno08x_tx_packet_t
{
    uint8_t body[30]; ///< Body of SHTP the packet (header + body)
    uint16_t length;  ///< Packet length in bytes.
} bno08x_tx_packet_t;

/// @brief Holds data that is received over spi.
typedef struct bno08x_rx_packet_t
{
    uint8_t header[4]; ///< Header of SHTP packet.
    uint8_t body[300]; /// Body of SHTP packet.
    uint16_t length;   ///< Packet length in bytes.
} bno08x_rx_packet_t;

/// @brief Data callback function prototype, void my_fxn(void *arg), where arg is a pointer to the BNO08x structure.
typedef void (*bno08x_cb_fxn_t)(void *);

/// @brief Holds any registered callbacks to be executed
typedef struct bno08x_cb_list_t
{
    bno08x_cb_fxn_t *callbacks; ///< dynamically allocated storage for callback functions
    uint16_t length; ///< length of the list (total amount of registered callback functions)
} bno08x_cb_list_t;

// Default IMU configuration settings modifiable via menuconfig
#define DEFAULT_IMU_CONFIG {CONFIG_ESP32_BNO08x_SPI_HOST, CONFIG_ESP32_BNO08X_GPIO_DI, CONFIG_ESP32_BNO08X_GPIO_SDA, CONFIG_ESP32_BNO08X_GPIO_SCL, CONFIG_ESP32_BNO08X_GPIO_CS, CONFIG_ESP32_BNO08X_GPIO_HINT, CONFIG_ESP32_BNO08X_GPIO_RST, CONFIG_ESP32_BNO08X_GPIO_WAKE, CONFIG_ESP32_BNO08X_SCL_SPEED_HZ, CONFIG_ESP32_BNO08X_SPI_INTR_CPU_AFFINITY}

typedef struct
{

    BNO08x_config_t imu_config;

    TaskHandle_t spi_task_hdl;
    TaskHandle_t data_proc_task_hdl;
    EventGroupHandle_t evt_grp_spi;
    EventGroupHandle_t evt_grp_report_en;
    QueueHandle_t queue_tx_data;
    QueueHandle_t queue_rx_data;
    QueueHandle_t queue_frs_read_data;
    QueueHandle_t queue_reset_reason;

    bno08x_cb_list_t *cb_list;

    uint8_t command_sequence_number;
    spi_bus_config_t bus_config;
    spi_device_interface_config_t imu_spi_config;
    spi_device_handle_t spi_hdl;
    spi_transaction_t spi_transaction;

    // Raw sensor values
    uint32_t time_stamp;
    uint16_t raw_accel_X, raw_accel_Y, raw_accel_Z, accel_accuracy;
    uint16_t raw_lin_accel_X, raw_lin_accel_Y, raw_lin_accel_Z, accel_lin_accuracy;
    uint16_t raw_gyro_X, raw_gyro_Y, raw_gyro_Z, gyro_accuracy;
    uint16_t raw_quat_I, raw_quat_J, raw_quat_K, raw_quat_real, raw_quat_radian_accuracy, quat_accuracy;
    uint16_t raw_velocity_gyro_X, raw_velocity_gyro_Y, raw_velocity_gyro_Z;
    uint16_t gravity_X, gravity_Y, gravity_Z, gravity_accuracy;
    uint16_t raw_uncalib_gyro_X, raw_uncalib_gyro_Y, raw_uncalib_gyro_Z, raw_bias_X, raw_bias_Y, raw_bias_Z, uncalib_gyro_accuracy;
    uint16_t raw_magf_X, raw_magf_Y, raw_magf_Z, magf_accuracy;
    uint8_t tap_detector;
    uint16_t step_count;
    int8_t stability_classifier;
    uint8_t activity_classifier;
    uint8_t *activity_confidences;
    uint8_t calibration_status;
    uint16_t mems_raw_accel_X, mems_raw_accel_Y, mems_raw_accel_Z;
    uint16_t mems_raw_gyro_X, mems_raw_gyro_Y, mems_raw_gyro_Z;
    uint16_t mems_raw_magf_X, mems_raw_magf_Y, mems_raw_magf_Z;
} BNO08x;

// Function prototypes
void BNO08x_init(BNO08x *device, BNO08x_config_t *imu_config);
bool BNO08x_initialize(BNO08x *device);

bool BNO08x_hard_reset(BNO08x *device);
bool BNO08x_soft_reset(BNO08x *device);
uint8_t BNO08x_get_reset_reason(BNO08x *device);

bool BNO08x_mode_sleep(BNO08x *device);
bool BNO08x_mode_on(BNO08x *device);
float BNO08x_q_to_float(int16_t fixed_point_value, uint8_t q_point);

bool BNO08x_run_full_calibration_routine(BNO08x *device);
void BNO08x_calibrate_all(BNO08x *device);
void BNO08x_calibrate_accelerometer(BNO08x *device);
void BNO08x_calibrate_gyro(BNO08x *device);
void BNO08x_calibrate_magnetometer(BNO08x *device);
void BNO08x_calibrate_planar_accelerometer(BNO08x *device);
void BNO08x_request_calibration_status(BNO08x *device);
bool BNO08x_calibration_complete(BNO08x *device);
void BNO08x_end_calibration(BNO08x *device);
void BNO08x_save_calibration(BNO08x *device);

void BNO08x_enable_rotation_vector(BNO08x *device, uint32_t time_between_reports);
void BNO08x_enable_game_rotation_vector(BNO08x *device, uint32_t time_between_reports);
void BNO08x_enable_ARVR_stabilized_rotation_vector(BNO08x *device, uint32_t time_between_reports);
void BNO08x_enable_ARVR_stabilized_game_rotation_vector(BNO08x *device, uint32_t time_between_reports);
void BNO08x_enable_gyro_integrated_rotation_vector(BNO08x *device, uint32_t time_between_reports);
void BNO08x_enable_accelerometer(BNO08x *device, uint32_t time_between_reports);
void BNO08x_enable_linear_accelerometer(BNO08x *device, uint32_t time_between_reports);
void BNO08x_enable_gravity(BNO08x *device, uint32_t time_between_reports);
void BNO08x_enable_gyro(BNO08x *device, uint32_t time_between_reports);
void BNO08x_enable_uncalibrated_gyro(BNO08x *device, uint32_t time_between_reports);
void BNO08x_enable_magnetometer(BNO08x *device, uint32_t time_between_reports);
void BNO08x_enable_tap_detector(BNO08x *device, uint32_t time_between_reports);
void BNO08x_enable_step_counter(BNO08x *device, uint32_t time_between_reports);
void BNO08x_enable_stability_classifier(BNO08x *device, uint32_t time_between_reports);
void BNO08x_enable_activity_classifier(BNO08x *device, uint32_t time_between_reports, uint32_t activities_to_enable, uint8_t activity_confidence_vals[9]);
void BNO08x_enable_raw_accelerometer(BNO08x *device, uint32_t time_between_reports);
void BNO08x_enable_raw_gyro(BNO08x *device, uint32_t time_between_reports);
void BNO08x_enable_raw_magnetometer(BNO08x *device, uint32_t time_between_reports);

void BNO08x_disable_rotation_vector(BNO08x *device);
void BNO08x_disable_game_rotation_vector(BNO08x *device);
void BNO08x_disable_ARVR_stabilized_rotation_vector(BNO08x *device);
void BNO08x_disable_ARVR_stabilized_game_rotation_vector(BNO08x *device);
void BNO08x_disable_gyro_integrated_rotation_vector(BNO08x *device);
void BNO08x_disable_accelerometer(BNO08x *device);
void BNO08x_disable_linear_accelerometer(BNO08x *device);
void BNO08x_disable_gravity(BNO08x *device);
void BNO08x_disable_gyro(BNO08x *device);
void BNO08x_disable_uncalibrated_gyro(BNO08x *device);
void BNO08x_disable_magnetometer(BNO08x *device);
void BNO08x_disable_tap_detector(BNO08x *device);
void BNO08x_disable_step_counter(BNO08x *device);
void BNO08x_disable_stability_classifier(BNO08x *device);
void BNO08x_disable_activity_classifier(BNO08x *device);
void BNO08x_disable_raw_accelerometer(BNO08x *device);
void BNO08x_disable_raw_gyro(BNO08x *device);
void BNO08x_disable_raw_magnetometer(BNO08x *device);

void BNO08x_tare_now(BNO08x *device, uint8_t axis_sel, uint8_t rotation_vector_basis);
void BNO08x_save_tare(BNO08x *device);
void BNO08x_clear_tare(BNO08x *device);

bool BNO08x_data_available(BNO08x *device);
bool BNO08x_register_cb(BNO08x *device, bno08x_cb_fxn_t cb_fxn);
bool BNO08x_clear_cb(BNO08x *device);

uint32_t BNO08x_get_time_stamp(BNO08x *device);

void BNO08x_get_magf(BNO08x *device, float *x, float *y, float *z, uint8_t *accuracy);
float BNO08x_get_magf_X(BNO08x *device);
float BNO08x_get_magf_Y(BNO08x *device);
float BNO08x_get_magf_Z(BNO08x *device);
uint8_t BNO08x_get_magf_accuracy(BNO08x *device);

void BNO08x_get_gravity(BNO08x *device, float *x, float *y, float *z, uint8_t *accuracy);
float BNO08x_get_gravity_X(BNO08x *device);
float BNO08x_get_gravity_Y(BNO08x *device);
float BNO08x_get_gravity_Z(BNO08x *device);
uint8_t BNO08x_get_gravity_accuracy(BNO08x *device);

float BNO08x_get_roll(BNO08x *device);
float BNO08x_get_pitch(BNO08x *device);
float BNO08x_get_yaw(BNO08x *device);

float BNO08x_get_roll_deg(BNO08x *device);
float BNO08x_get_pitch_deg(BNO08x *device);
float BNO08x_get_yaw_deg(BNO08x *device);

void BNO08x_get_quat(BNO08x *device, float *i, float *j, float *k, float *real, float *rad_accuracy, uint8_t *accuracy);
float BNO08x_get_quat_I(BNO08x *device);
float BNO08x_get_quat_J(BNO08x *device);
float BNO08x_get_quat_K(BNO08x *device);
float BNO08x_get_quat_real(BNO08x *device);
float BNO08x_get_quat_radian_accuracy(BNO08x *device);
uint8_t BNO08x_get_quat_accuracy(BNO08x *device);

void BNO08x_get_accel(BNO08x *device, float *x, float *y, float *z, uint8_t *accuracy);
float BNO08x_get_accel_X(BNO08x *device);
float BNO08x_get_accel_Y(BNO08x *device);
float BNO08x_get_accel_Z(BNO08x *device);
uint8_t BNO08x_get_accel_accuracy(BNO08x *device);

void BNO08x_get_linear_accel(BNO08x *device, float *x, float *y, float *z, uint8_t *accuracy);
float BNO08x_get_linear_accel_X(BNO08x *device);
float BNO08x_get_linear_accel_Y(BNO08x *device);
float BNO08x_get_linear_accel_Z(BNO08x *device);
uint8_t BNO08x_get_linear_accel_accuracy(BNO08x *device);

int16_t BNO08x_get_raw_accel_X(BNO08x *device);
int16_t BNO08x_get_raw_accel_Y(BNO08x *device);
int16_t BNO08x_get_raw_accel_Z(BNO08x *device);

int16_t BNO08x_get_raw_gyro_X(BNO08x *device);
int16_t BNO08x_get_raw_gyro_Y(BNO08x *device);
int16_t BNO08x_get_raw_gyro_Z(BNO08x *device);

int16_t BNO08x_get_raw_magf_X(BNO08x *device);
int16_t BNO08x_get_raw_magf_Y(BNO08x *device);
int16_t BNO08x_get_raw_magf_Z(BNO08x *device);

void BNO08x_get_gyro_calibrated_velocity(BNO08x *device, float *x, float *y, float *z, uint8_t *accuracy);
float BNO08x_get_gyro_calibrated_velocity_X(BNO08x *device);
float BNO08x_get_gyro_calibrated_velocity_Y(BNO08x *device);
float BNO08x_get_gyro_calibrated_velocity_Z(BNO08x *device);
uint8_t BNO08x_get_gyro_accuracy(BNO08x *device);

void BNO08x_get_uncalibrated_gyro(BNO08x *device, float *x, float *y, float *z, float *bx, float *by, float *bz, uint8_t *accuracy);
float BNO08x_get_uncalibrated_gyro_X(BNO08x *device);
float BNO08x_get_uncalibrated_gyro_Y(BNO08x *device);
float BNO08x_get_uncalibrated_gyro_Z(BNO08x *device);
float BNO08x_get_uncalibrated_gyro_bias_X(BNO08x *device);
float BNO08x_get_uncalibrated_gyro_bias_Y(BNO08x *device);
float BNO08x_get_uncalibrated_gyro_bias_Z(BNO08x *device);
uint8_t BNO08x_get_uncalibrated_gyro_accuracy(BNO08x *device);

void BNO08x_get_gyro_velocity(BNO08x *device, float *x, float *y, float *z);
float BNO08x_get_gyro_velocity_X(BNO08x *device);
float BNO08x_get_gyro_velocity_Y(BNO08x *device);
float BNO08x_get_gyro_velocity_Z(BNO08x *device);

uint8_t BNO08x_get_tap_detector(BNO08x *device);
uint16_t BNO08x_get_step_count(BNO08x *device);
int8_t BNO08x_get_stability_classifier(BNO08x *device);
uint8_t BNO08x_get_activity_classifier(BNO08x *device);

void BNO08x_print_header(BNO08x *device, bno08x_rx_packet_t *packet);
void BNO08x_print_packet(BNO08x *device, bno08x_rx_packet_t *packet);

// Metadata functions
int16_t BNO08x_get_Q1(BNO08x *device, uint16_t record_ID);
int16_t BNO08x_get_Q2(BNO08x *device, uint16_t record_ID);
int16_t BNO08x_get_Q3(BNO08x *device, uint16_t record_ID);
float BNO08x_get_resolution(BNO08x *device, uint16_t record_ID);
float BNO08x_get_range(BNO08x *device, uint16_t record_ID);
uint32_t BNO08x_FRS_read_word(BNO08x *device, uint16_t record_ID, uint8_t word_number);
bool BNO08x_FRS_read_request(BNO08x *device, uint16_t record_ID, uint16_t read_offset, uint16_t block_size);
bool BNO08x_FRS_read_data(BNO08x *device, uint16_t record_ID, uint8_t start_location, uint8_t words_to_read, uint32_t *meta_data);

// Private functions
bool BNO08x_wait_for_rx_done(BNO08x *device);
bool BNO08x_wait_for_tx_done(BNO08x *device);
bool BNO08x_wait_for_data(BNO08x *device);

bool BNO08x_receive_packet(BNO08x *device);
void BNO08x_send_packet(BNO08x *device, bno08x_tx_packet_t *packet);
void BNO08x_queue_packet(BNO08x *device, uint8_t channel_number, uint8_t data_length, uint8_t *commands);
void BNO08x_queue_command(BNO08x *device, uint8_t command, uint8_t *commands);
void BNO08x_queue_feature_command(BNO08x *device, uint8_t report_ID, uint32_t time_between_reports, uint32_t specific_config);
void BNO08x_queue_calibrate_command(BNO08x *device, uint8_t _to_calibrate);
void BNO08x_queue_tare_command(BNO08x *device, uint8_t command, uint8_t axis, uint8_t rotation_vector_basis);
void BNO08x_queue_request_product_id_command(BNO08x *device);

void BNO08x_enable_report(BNO08x *device, uint8_t report_ID, uint32_t time_between_reports, const EventBits_t report_evt_grp_bit, uint32_t specific_config);
void BNO08x_disable_report(BNO08x *device, uint8_t report_ID, const EventBits_t report_evt_grp_bit);
uint16_t BNO08x_parse_packet(BNO08x *device, bno08x_rx_packet_t *packet);
uint16_t BNO08x_parse_product_id_report(BNO08x *device, bno08x_rx_packet_t *packet);
uint16_t BNO08x_parse_frs_read_response_report(BNO08x *device, bno08x_rx_packet_t *packet);
uint16_t BNO08x_parse_input_report(BNO08x *device, bno08x_rx_packet_t *packet);
uint16_t BNO08x_parse_command_report(BNO08x *device, bno08x_rx_packet_t *packet);

// Max RX buffer space
#define RX_DATA_LENGTH 300

// Max meta-data length (from FRS reads)
#define MAX_METADATA_LENGTH 9

// Record IDs
#define FRS_RECORD_ID_ACCELEROMETER 0xE302
#define FRS_RECORD_ID_GYROSCOPE_CALIBRATED 0xE306
#define FRS_RECORD_ID_MAGNETIC_FIELD_CALIBRATED 0xE309
#define FRS_RECORD_ID_ROTATION_VECTOR 0xE30B

// Activity classifier enable bits
#define ACTIVITY_CLASSIFIER_UNKNOWN_EN (1 << 0)
#define ACTIVITY_CLASSIFIER_IN_VEHICLE_EN (1 << 1)
#define ACTIVITY_CLASSIFIER_ON_BICYCLE_EN (1 << 2)
#define ACTIVITY_CLASSIFIER_ON_FOOT_EN (1 << 3)
#define ACTIVITY_CLASSIFIER_STILL_EN (1 << 4)
#define ACTIVITY_CLASSIFIER_TILTING_EN (1 << 5)
#define ACTIVITY_CLASSIFIER_WALKING_EN (1 << 6)
#define ACTIVITY_CLASSIFIER_RUNNING_EN (1 << 7)
#define ACTIVITY_CLASSIFIER_ON_STAIRS_EN (1 << 8)
#define ACTIVITY_CLASSIFIER_ALL_EN 0x1F

// Tare commands
#define TARE_AXIS_ALL 0x07
#define TARE_AXIS_Z 0x04

#define TARE_ROTATION_VECTOR 0
#define TARE_GAME_ROTATION_VECTOR 1
#define TARE_GEOMAGNETIC_ROTATION_VECTOR 2
#define TARE_GYRO_INTEGRATED_ROTATION_VECTOR 3
#define TARE_ARVR_STABILIZED_ROTATION_VECTOR 4
#define TARE_ARVR_STABILIZED_GAME_ROTATION_VECTOR 5

#define ROTATION_VECTOR_Q1 14
#define ROTATION_VECTOR_ACCURACY_Q1 12
#define ACCELEROMETER_Q1 8
#define LINEAR_ACCELEROMETER_Q1 8
#define GYRO_Q1 9
#define MAGNETOMETER_Q1 4
#define ANGULAR_VELOCITY_Q1 10
#define GRAVITY_Q1 8

// Higher level calibration commands
#define CALIBRATE_ACCEL 0
#define CALIBRATE_GYRO 1
#define CALIBRATE_MAG 2
#define CALIBRATE_PLANAR_ACCEL 3
#define CALIBRATE_ACCEL_GYRO_MAG 4
#define CALIBRATE_STOP 5

// Command IDs
#define COMMAND_ERRORS 1
#define COMMAND_COUNTER 2
#define COMMAND_TARE 3
#define COMMAND_INITIALIZE 4
#define COMMAND_DCD 6
#define COMMAND_ME_CALIBRATE 7
#define COMMAND_DCD_PERIOD_SAVE 9
#define COMMAND_OSCILLATOR 10
#define COMMAND_CLEAR_DCD 11

// SHTP channel 2 control report IDs
#define SHTP_REPORT_COMMAND_RESPONSE 0xF1
#define SHTP_REPORT_COMMAND_REQUEST 0xF2
#define SHTP_REPORT_FRS_READ_RESPONSE 0xF3
#define SHTP_REPORT_FRS_READ_REQUEST 0xF4
#define SHTP_REPORT_PRODUCT_ID_RESPONSE 0xF8
#define SHTP_REPORT_PRODUCT_ID_REQUEST 0xF9
#define SHTP_REPORT_BASE_TIMESTAMP 0xFB
#define SHTP_REPORT_SET_FEATURE_COMMAND 0xFD

// Sensor report IDs
#define SENSOR_REPORT_ID_ACCELEROMETER 0x01
#define SENSOR_REPORT_ID_GYROSCOPE 0x02
#define SENSOR_REPORT_ID_MAGNETIC_FIELD 0x03
#define SENSOR_REPORT_ID_LINEAR_ACCELERATION 0x04
#define SENSOR_REPORT_ID_ROTATION_VECTOR 0x05
#define SENSOR_REPORT_ID_GRAVITY 0x06
#define SENSOR_REPORT_ID_UNCALIBRATED_GYRO 0x07
#define SENSOR_REPORT_ID_GAME_ROTATION_VECTOR 0x08
#define SENSOR_REPORT_ID_GEOMAGNETIC_ROTATION_VECTOR 0x09
#define SENSOR_REPORT_ID_GYRO_INTEGRATED_ROTATION_VECTOR 0x2A
#define SENSOR_REPORT_ID_TAP_DETECTOR 0x10
#define SENSOR_REPORT_ID_STEP_COUNTER 0x11
#define SENSOR_REPORT_ID_STABILITY_CLASSIFIER 0x13
#define SENSOR_REPORT_ID_RAW_ACCELEROMETER 0x14
#define SENSOR_REPORT_ID_RAW_GYROSCOPE 0x15
#define SENSOR_REPORT_ID_RAW_MAGNETOMETER 0x16
#define SENSOR_REPORT_ID_PERSONAL_ACTIVITY_CLASSIFIER 0x1E
#define SENSOR_REPORT_ID_ARVR_STABILIZED_ROTATION_VECTOR 0x28
#define SENSOR_REPORT_ID_ARVR_STABILIZED_GAME_ROTATION_VECTOR 0x29

// Tare commands
#define TARE_NOW 0
#define TARE_PERSIST 1
#define TARE_SET_REORIENTATION 2

#define HOST_INT_TIMEOUT_MS 300ULL

// ISR service installation flag
extern bool bno08x_isr_service_installed;

// Function prototypes for ISR and task handling
void IRAM_ATTR BNO08x_hint_handler(void *arg);
void BNO08x_spi_task(void *arg);
void BNO08x_data_proc_task(void *arg);

#endif