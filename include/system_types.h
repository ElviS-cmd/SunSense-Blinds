/**
 * @file system_types.h
 * @brief System Types and Shared Enumerations
 * @author Elvis
 * @date 2026
 * 
 * Shared data types used across all controllers
 * Prevents enum duplication and ensures consistency
 */

#ifndef SYSTEM_TYPES_H
#define SYSTEM_TYPES_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * OPERATING MODES
 * ========================================================================== */

typedef enum {
    MODE_AUTO = 0,      // Automatic light-responsive mode
    MODE_MANUAL = 1     // Manual button control mode
} OperatingMode_t;

/* ============================================================================
 * MOTOR STATES
 * ========================================================================== */

typedef enum {
    MOTOR_STOP = 0,      // Motor not running
    MOTOR_OPENING = 1,   // Motor running forward (open blinds)
    MOTOR_CLOSING = 2    // Motor running reverse (close blinds)
} MotorState_t;

/* ============================================================================
 * BUTTON ACTIONS
 * ========================================================================== */

typedef enum {
    BUTTON_ACTION_NONE = 0,     // No action
    BUTTON_ACTION_SHORT = 1,    // Short press (< 2s)
    BUTTON_ACTION_LONG = 2      // Long press (>= 2s)
} ButtonAction_t;

/* ============================================================================
 * LIGHT LEVELS
 * ========================================================================== */

typedef enum {
    LIGHT_DARK = 0,     // Low light (below dark threshold)
    LIGHT_BRIGHT = 1    // High light (above bright threshold)
} LightLevel_t;

/* ============================================================================
 * LED STATES
 * ========================================================================== */

typedef enum {
    LED_OFF = 0,        // LED off
    LED_ON = 1,         // LED solid on
    LED_BLINK_SLOW = 2, // Blink slow (500ms period)
    LED_BLINK_FAST = 3, // Blink fast (250ms period)
    LED_PULSE = 4       // Pulse effect
} LEDState_t;

/* ============================================================================
 * ENCODER/SERVO STATES
 * ========================================================================== */

typedef enum {
    SERVO_IDLE = 0,         // Servo at rest
    SERVO_MOVING = 1        // Servo moving to target
} ServoState_t;

typedef enum {
    ENCODER_STATUS_OK = 0,          // Normal operation
    ENCODER_STATUS_MAGNET_WEAK = 1, // Magnet too far
    ENCODER_STATUS_MAGNET_STRONG = 2, // Magnet too close
    ENCODER_STATUS_ERROR = 3        // I2C error
} EncoderStatus_t;

/* ============================================================================
 * SYSTEM EVENTS
 * ========================================================================== */

typedef enum {
    EVENT_NONE = 0,
    EVENT_BUTTON_PRESSED = 1,
    EVENT_MODE_CHANGED = 2,
    EVENT_LIGHT_CHANGED = 3,
    EVENT_MOTOR_STARTED = 4,
    EVENT_MOTOR_STOPPED = 5,
    EVENT_ENCODER_ERROR = 6,
    EVENT_MICROPHONE_READY = 7
} SystemEvent_t;

/* ============================================================================
 * SYSTEM COMMANDS
 * ========================================================================== */

typedef enum {
    COMMAND_NONE = 0,
    COMMAND_OPEN,
    COMMAND_CLOSE,
    COMMAND_STOP,
    COMMAND_SET_AUTO,
    COMMAND_SET_MANUAL,
    COMMAND_RETURN_TO_AUTO,
    COMMAND_STOP_ALL
} SystemCommand_t;

typedef enum {
    COMMAND_SOURCE_NONE = 0,
    COMMAND_SOURCE_BUTTON,
    COMMAND_SOURCE_MQTT,
    COMMAND_SOURCE_MICROPHONE
} CommandSource_t;

/* ============================================================================
 * SHARED STRUCTURES
 * ========================================================================== */

/**
 * @brief Event queue entry for inter-task communication
 */
typedef struct {
    SystemEvent_t event_type;
    uint32_t timestamp;
    union {
        uint16_t value_u16;
        int16_t value_i16;
        uint32_t value_u32;
        float value_f32;
        void *pointer;
    } data;
} SystemEvent_Queue_t;

typedef struct {
    SystemCommand_t command;
    CommandSource_t source;
    uint32_t timestamp;
} SystemCommandMessage_t;

/**
 * @brief System configuration (could be persisted to NVS)
 */
typedef struct {
    uint16_t ldr_dark_threshold;    // ADC value for dark
    uint16_t ldr_bright_threshold;  // ADC value for bright
    uint16_t ldr_hysteresis;        // Hysteresis band
    uint16_t audio_threshold;       // RMS threshold for sound detection
    uint32_t motor_timeout_ms;      // Max motor run time
    uint32_t servo_speed_ms;        // Servo movement speed
    bool enable_voice_commands;     // Enable audio processing
} SystemConfig_t;

/**
 * @brief Global system health status
 */
typedef struct {
    bool button_ok;
    bool motor_ok;
    bool led_ok;
    bool ldr_ok;
    bool encoder_ok;
    bool servo_ok;
    bool microphone_ok;
    uint32_t error_count;
    uint32_t last_error_time;
} SystemHealth_t;

/* ============================================================================
 * FUNCTION DECLARATIONS
 * ========================================================================== */

/**
 * @brief Convert operating mode to string
 * @param mode Operating mode enum
 * @return String representation
 */
const char* mode_to_string(OperatingMode_t mode);

/**
 * @brief Convert motor state to string
 * @param state Motor state enum
 * @return String representation
 */
const char* motor_state_to_string(MotorState_t state);

/**
 * @brief Convert button action to string
 * @param action Button action enum
 * @return String representation
 */
const char* button_action_to_string(ButtonAction_t action);

/**
 * @brief Convert light level to string
 * @param level Light level enum
 * @return String representation
 */
const char* light_level_to_string(LightLevel_t level);

/**
 * @brief Convert encoder status to string
 * @param status Encoder status enum
 * @return String representation
 */
const char* encoder_status_to_string(EncoderStatus_t status);

/**
 * @brief Convert system command to string
 * @param command System command enum
 * @return String representation
 */
const char* system_command_to_string(SystemCommand_t command);

/**
 * @brief Get default system configuration
 * @return SystemConfig_t with default values
 */
SystemConfig_t get_default_config(void);

#ifdef __cplusplus
}
#endif

#endif /* SYSTEM_TYPES_H */
