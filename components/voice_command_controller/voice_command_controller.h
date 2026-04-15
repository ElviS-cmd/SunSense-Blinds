/**
 * @file voice_command_controller.h
 * @brief Lightweight local voice-utterance command recognizer for SunSense V2
 *
 * This is not semantic speech recognition. It converts spoken/loud utterance
 * bursts from the microphone RMS level into simple commands so the audio path
 * can be tested locally before adding a keyword model.
 */

#ifndef VOICE_COMMAND_CONTROLLER_H
#define VOICE_COMMAND_CONTROLLER_H

#include "system_types.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool active;
    uint8_t utterance_count;
    uint32_t utterance_start_time;
    uint32_t first_utterance_time;
    uint32_t last_active_time;
    uint32_t cooldown_start_time;   /* time cooldown began; 0 = no active cooldown */
    uint16_t last_level;
} VoiceCommandController_t;

bool voice_command_init(VoiceCommandController_t *voice);

SystemCommand_t voice_command_update(VoiceCommandController_t *voice,
                                     uint16_t audio_level,
                                     uint16_t threshold,
                                     uint32_t current_time);

const char *voice_command_to_string(SystemCommand_t command);

#ifdef __cplusplus
}
#endif

#endif /* VOICE_COMMAND_CONTROLLER_H */
