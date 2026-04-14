/**
 * @file voice_command_controller.c
 * @brief Lightweight local voice-utterance command recognizer implementation
 */

#include "voice_command_controller.h"
#include "gpio_config.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "Voice";

bool voice_command_init(VoiceCommandController_t *voice) {
    if (voice == NULL) {
        return false;
    }

    memset(voice, 0, sizeof(VoiceCommandController_t));
    return true;
}

static void reset_sequence(VoiceCommandController_t *voice) {
    voice->active = false;
    voice->utterance_count = 0U;
    voice->utterance_start_time = 0U;
    voice->first_utterance_time = 0U;
    voice->last_active_time = 0U;
}

static SystemCommand_t command_from_utterance_count(uint8_t count) {
    switch (count) {
        case 1:
            return COMMAND_OPEN;
        case 2:
            return COMMAND_CLOSE;
        case 3:
            return COMMAND_STOP;
        case 4:
            return COMMAND_RETURN_TO_AUTO;
        default:
            return COMMAND_NONE;
    }
}

const char *voice_command_to_string(SystemCommand_t command) {
    switch (command) {
        case COMMAND_OPEN:
            return "OPEN";
        case COMMAND_CLOSE:
            return "CLOSE";
        case COMMAND_STOP:
            return "STOP";
        case COMMAND_RETURN_TO_AUTO:
            return "AUTO";
        default:
            return "NONE";
    }
}

SystemCommand_t voice_command_update(VoiceCommandController_t *voice,
                                     uint16_t audio_level,
                                     uint16_t threshold,
                                     uint32_t current_time) {
    if (voice == NULL) {
        return COMMAND_NONE;
    }

    voice->last_level = audio_level;

    if (current_time < voice->cooldown_until_time) {
        return COMMAND_NONE;
    }

    bool loud = (audio_level >= threshold);

    if (loud) {
        if (!voice->active) {
            voice->active = true;
            voice->utterance_start_time = current_time;
            if (voice->utterance_count == 0U) {
                voice->first_utterance_time = current_time;
            }
            ESP_LOGI(TAG, "Utterance started level=%u threshold=%u", audio_level, threshold);
        }
        voice->last_active_time = current_time;
        return COMMAND_NONE;
    }

    if (voice->active &&
        ((current_time - voice->last_active_time) >= VOICE_QUIET_GAP_MS)) {
        uint32_t duration = voice->last_active_time - voice->utterance_start_time;
        voice->active = false;

        if (duration >= VOICE_UTTERANCE_MIN_MS && duration <= VOICE_UTTERANCE_MAX_MS) {
            if (voice->utterance_count < VOICE_MAX_UTTERANCES) {
                voice->utterance_count++;
            }
            ESP_LOGI(TAG,
                     "Utterance accepted count=%u duration=%lums",
                     voice->utterance_count,
                     (unsigned long)duration);
        } else {
            ESP_LOGI(TAG,
                     "Utterance ignored duration=%lums expected=%u-%ums",
                     (unsigned long)duration,
                     VOICE_UTTERANCE_MIN_MS,
                     VOICE_UTTERANCE_MAX_MS);
        }
    }

    if (voice->utterance_count == 0U) {
        return COMMAND_NONE;
    }

    bool quiet_after_sequence =
        (voice->last_active_time != 0U) &&
        ((current_time - voice->last_active_time) >= VOICE_COMMAND_FINAL_QUIET_MS);
    bool window_expired =
        (voice->first_utterance_time != 0U) &&
        ((current_time - voice->first_utterance_time) >= VOICE_COMMAND_WINDOW_MS);

    if (!quiet_after_sequence && !window_expired) {
        return COMMAND_NONE;
    }

    SystemCommand_t command = command_from_utterance_count(voice->utterance_count);
    if (command == COMMAND_NONE) {
        ESP_LOGI(TAG, "Voice sequence ignored count=%u", voice->utterance_count);
        reset_sequence(voice);
        return COMMAND_NONE;
    }

    ESP_LOGI(TAG,
             "Voice command recognized command=%s count=%u",
             voice_command_to_string(command),
             voice->utterance_count);

    reset_sequence(voice);
    voice->cooldown_until_time = current_time + VOICE_COMMAND_COOLDOWN_MS;
    return command;
}
