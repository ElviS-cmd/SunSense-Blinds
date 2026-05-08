/**
 * @file mqtt_handlers.h
 * @brief MQTT command handlers and state publishing declarations
 */
#pragma once

#include <stdint.h>
#include "esp_event.h"
#include "app_controller.h"
#include "app_state.h"

/* ============================================================================
 * FUNCTIONS DEFINED IN mqtt_handlers.cpp
 * ========================================================================== */

void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                        int32_t event_id, void *event_data);
void publish_state_if_ready(void);
