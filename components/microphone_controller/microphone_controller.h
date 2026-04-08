/**
 * @file microphone_controller.h
 * @brief Microphone Controller for SunSense V2
 * @author Elvis
 * @date 2026
 * 
 * INMP441 MEMS microphone via I2S interface
 * Captures audio for voice command recognition (future feature)
 */

#ifndef MICROPHONE_CONTROLLER_H
#define MICROPHONE_CONTROLLER_H

#include "system_types.h"
#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * MICROPHONE AUDIO BUFFER
 * ========================================================================== */

#define MIC_SAMPLE_RATE         16000       // 16 kHz
#define MIC_BITS_PER_SAMPLE     16          // 16-bit PCM
#define MIC_BUFFER_SIZE         512         // Samples per buffer

/* ============================================================================
 * MICROPHONE CONTROLLER STRUCT
 * ========================================================================== */

typedef struct {
    int16_t audio_buffer[MIC_BUFFER_SIZE];  // PCM audio samples
    uint32_t buffer_index;                  // Current position in buffer
    uint32_t sample_count;                  // Total samples collected
    bool buffer_ready;                      // Is buffer full and ready for processing?
    uint32_t last_sample_time;              // Last sample timestamp
} MicrophoneController_t;

/* ============================================================================
 * FUNCTION DECLARATIONS
 * ========================================================================== */

/**
 * @brief Initialize microphone controller and I2S interface
 * @param mic Pointer to MicrophoneController_t structure
 * @return true if initialization successful, false otherwise
 */
bool microphone_init(MicrophoneController_t *mic);

/**
 * @brief Read audio samples from I2S and fill buffer
 * @param mic Pointer to MicrophoneController_t structure
 * @param current_time Current time in milliseconds
 * @return true if read successful, false otherwise
 */
bool microphone_update(MicrophoneController_t *mic, uint32_t current_time);

/**
 * @brief Get audio buffer pointer
 * @param mic Pointer to MicrophoneController_t structure
 * @return Pointer to audio_buffer array
 */
int16_t* microphone_get_buffer(MicrophoneController_t *mic);

/**
 * @brief Check if audio buffer is ready
 * @param mic Pointer to MicrophoneController_t structure
 * @return true if buffer is full and ready for processing
 */
bool microphone_is_buffer_ready(MicrophoneController_t *mic);

/**
 * @brief Clear audio buffer and reset
 * @param mic Pointer to MicrophoneController_t structure
 */
void microphone_clear_buffer(MicrophoneController_t *mic);

/**
 * @brief Get current audio level (RMS)
 * @param mic Pointer to MicrophoneController_t structure
 * @return RMS level of current buffer (0-32767)
 */
uint16_t microphone_get_level(MicrophoneController_t *mic);

/**
 * @brief Check if sound is detected
 * @param mic Pointer to MicrophoneController_t structure
 * @param threshold RMS threshold for detection
 * @return true if audio level > threshold
 */
bool microphone_sound_detected(MicrophoneController_t *mic, uint16_t threshold);

/**
 * @brief Get total samples collected
 * @param mic Pointer to MicrophoneController_t structure
 * @return Total sample count
 */
uint32_t microphone_get_sample_count(MicrophoneController_t *mic);

#endif /* MICROPHONE_CONTROLLER_H */
