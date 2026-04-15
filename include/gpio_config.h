/**
 * @file gpio_config.h
 * @brief SunSense V2 GPIO Configuration
 * @author Elvis
 * @date 2026
 *
 * Complete GPIO pin definitions and peripheral configurations for SunSense V2
 * ESP32-S3-Zero with simplified hardware: 1 button, 2 LEDs, microphone, encoder, servo, motor
 */

#ifndef GPIO_CONFIG_H
#define GPIO_CONFIG_H

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "driver/ledc.h"

/* ============================================================================
 * CONTROL & STATUS PINS
 * ========================================================================== */

/* Button Input */
#define GPIO_BUTTON             GPIO_NUM_3

/* LED Outputs */
#define GPIO_LED_GREEN          GPIO_NUM_5
#define GPIO_LED_BLUE           GPIO_NUM_6

/* ============================================================================
 * I2C INTERFACE (AS5600 Magnetic Encoder)
 * ========================================================================== */

#define GPIO_I2C_SDA            GPIO_NUM_8
#define GPIO_I2C_SCL            GPIO_NUM_9

#define I2C_MASTER_PORT         I2C_NUM_0
#define I2C_MASTER_FREQ_HZ      100000
#define I2C_TRANSACTION_TIMEOUT_MS 50

#define AS5600_I2C_ADDR         0x36
#define AS5600_REG_RAW_ANGLE    0x0C
#define AS5600_REG_STATUS       0x0B
#define AS5600_REG_AGC          0x0A

/* ============================================================================
 * I2S INTERFACE (INMP441 MEMS Microphone)
 * ========================================================================== */

#define GPIO_I2S_CLK            GPIO_NUM_2
#define GPIO_I2S_WS             GPIO_NUM_4
#define GPIO_I2S_SD             GPIO_NUM_7

#define I2S_PORT                I2S_NUM_0
#define I2S_SAMPLE_RATE         16000
#define I2S_BITS_PER_SAMPLE     I2S_BITS_PER_SAMPLE_16BIT
#define I2S_CHANNELS            1
#define I2S_DMA_BUF_COUNT       8
#define I2S_DMA_BUF_LEN         64

#define INMP441_LEFT_CHANNEL    1

/* ============================================================================
 * PWM OUTPUTS (Servo Motor)
 * ========================================================================== */

#define GPIO_SERVO              GPIO_NUM_10

#define SERVO_PWM_FREQ          50
#define SERVO_PWM_RESOLUTION    LEDC_TIMER_12_BIT
#define SERVO_PWM_MODE          LEDC_LOW_SPEED_MODE
#define SERVO_PWM_TIMER         LEDC_TIMER_0
#define SERVO_PWM_CHANNEL       LEDC_CHANNEL_0

#define SERVO_PWM_MIN_US        1000
#define SERVO_PWM_MID_US        1500
#define SERVO_PWM_MAX_US        2000

/* Calibrated safe range for the installed linkage. Avoid the MG996R endpoints. */
#define SERVO_SLAT_CLOSED_ANGLE 60.0f
#define SERVO_SLAT_OPEN_ANGLE   120.0f
/* Slat tilt is blocked only when the blinds are effectively fully rolled up. */
#define SERVO_TILT_BLOCKED_POSITION_MIN_PERCENT 95U
#define SERVO_RAMP_DURATION_MS  3000U
#define SERVO_RAMP_PERIOD_MS    40U
#define SERVO_SETTLE_MS         400U

#define SERVO_DUTY_MIN          ((SERVO_PWM_MIN_US * 4095) / 20000)
#define SERVO_DUTY_MID          ((SERVO_PWM_MID_US * 4095) / 20000)
#define SERVO_DUTY_MAX          ((SERVO_PWM_MAX_US * 4095) / 20000)

/* Set to 1 to compile a servo-only ESP-IDF test app path in main.cpp. */
#define SUNSENSE_SERVO_TEST_ONLY 0
#define SUNSENSE_SERVO_TEST_FULL_RANGE 0
#define SUNSENSE_SERVO_BOOT_EXERCISE 0
#define SERVO_TEST_HOLD_MS       2000

/* ============================================================================
 * VOICE COMMAND RECOGNITION
 * ========================================================================== */

/*
 * First-pass local voice command mode uses utterance bursts above RMS threshold:
 * 1 utterance = open, 2 = close, 3 = stop, 4 = return to auto.
 */
#define VOICE_UTTERANCE_MIN_MS        120
#define VOICE_UTTERANCE_MAX_MS        1200
#define VOICE_QUIET_GAP_MS            250
#define VOICE_COMMAND_FINAL_QUIET_MS  800
#define VOICE_COMMAND_WINDOW_MS       3500
#define VOICE_COMMAND_COOLDOWN_MS     2500
#define VOICE_MAX_UTTERANCES          4

/* ============================================================================
 * MOTOR CONTROL (L298N Motor Driver)
 * ========================================================================== */

#define GPIO_MOTOR_IN1          GPIO_NUM_12
#define GPIO_MOTOR_IN2          GPIO_NUM_13

/* ============================================================================
 * ADC INPUT (LDR Light Sensor)
 * ========================================================================== */

#define GPIO_LDR                GPIO_NUM_1

#define ADC_PORT                ADC_UNIT_1
#define ADC_CHANNEL             ADC_CHANNEL_0
#define ADC_RESOLUTION          ADC_BITWIDTH_12
#define ADC_ATTEN               ADC_ATTEN_DB_12

/* Lower ADC values mean more light on this module, higher values mean darker. */
#define LDR_BRIGHT_THRESHOLD    1200
#define LDR_DARK_THRESHOLD      1800
#define LDR_HYSTERESIS          600

/* ============================================================================
 * FreeRTOS TASK CONFIGURATION
 * ========================================================================== */

#define TASK_STACK_BUTTON       4096
#define TASK_STACK_MODE         4096
#define TASK_STACK_MOTOR        4096
#define TASK_STACK_LED          3072
#define TASK_STACK_LDR          4096
#define TASK_STACK_ENCODER      4096
#define TASK_STACK_MICROPHONE   6144

#define TASK_PRIORITY_BUTTON    6
#define TASK_PRIORITY_MODE      5
#define TASK_PRIORITY_MOTOR     5
#define TASK_PRIORITY_LED       3
#define TASK_PRIORITY_LDR       5
#define TASK_PRIORITY_ENCODER   4
#define TASK_PRIORITY_MICROPHONE 6

#define TASK_PERIOD_BUTTON      50
#define TASK_PERIOD_MODE        100
#define TASK_PERIOD_MOTOR       100
#define TASK_PERIOD_LED         100
#define TASK_PERIOD_LDR         1000
#define TASK_PERIOD_ENCODER     100
#define TASK_PERIOD_MICROPHONE  20

/* ============================================================================
 * SYSTEM CONSTANTS
 * ========================================================================== */

#define BUTTON_DEBOUNCE_MS      50
#define BUTTON_HOLD_TIME_MS     2000

#define LED_BLINK_NORMAL        500
#define LED_BLINK_CONNECTED     1000
#define LED_PULSE_SPEED         100

#define SERVO_SPEED_SLOW        50
#define SERVO_SPEED_FAST        20

#define MOTOR_RAMP_TIME         500
#define MOTOR_TRAVEL_TIME_MS    120000U

/* ============================================================================
 * RUNTIME PERSISTENCE / RECOVERY
 * ========================================================================== */

#define RUNTIME_POSITION_SAVE_DELTA     5U
#define RUNTIME_POSITION_SAVE_INTERVAL_MS 5000U
#define AUTO_STARTUP_SETTLE_MS          3000U

#endif /* GPIO_CONFIG_H */
