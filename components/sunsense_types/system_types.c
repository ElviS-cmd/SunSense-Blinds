#include "system_types.h"

const char* mode_to_string(OperatingMode_t mode) {
    switch (mode) {
        case MODE_AUTO:
            return "AUTO";
        case MODE_MANUAL:
            return "MANUAL";
        default:
            return "UNKNOWN";
    }
}

const char* motor_state_to_string(MotorState_t state) {
    switch (state) {
        case MOTOR_STOP:
            return "STOP";
        case MOTOR_OPENING:
            return "OPENING";
        case MOTOR_CLOSING:
            return "CLOSING";
        default:
            return "UNKNOWN";
    }
}

const char* button_action_to_string(ButtonAction_t action) {
    switch (action) {
        case BUTTON_ACTION_NONE:
            return "NONE";
        case BUTTON_ACTION_SHORT:
            return "SHORT_PRESS";
        case BUTTON_ACTION_LONG:
            return "LONG_PRESS";
        default:
            return "UNKNOWN";
    }
}

const char* light_level_to_string(LightLevel_t level) {
    switch (level) {
        case LIGHT_DARK:
            return "DARK";
        case LIGHT_BRIGHT:
            return "BRIGHT";
        default:
            return "UNKNOWN";
    }
}

const char* encoder_status_to_string(EncoderStatus_t status) {
    switch (status) {
        case ENCODER_STATUS_OK:
            return "OK";
        case ENCODER_STATUS_MAGNET_WEAK:
            return "MAGNET_WEAK";
        case ENCODER_STATUS_MAGNET_STRONG:
            return "MAGNET_STRONG";
        case ENCODER_STATUS_ERROR:
            return "ERROR";
        default:
            return "UNKNOWN";
    }
}

SystemConfig_t get_default_config(void) {
    SystemConfig_t config = {
        .ldr_dark_threshold = 1500,
        .ldr_bright_threshold = 2500,
        .ldr_hysteresis = 200,
        .audio_threshold = 1000,
        .motor_timeout_ms = 30000,
        .servo_speed_ms = 50,
        .enable_voice_commands = true
    };
    return config;
}
