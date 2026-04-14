/**
 * @file network.h
 * @brief Network configuration, BLE provisioning, and connectivity declarations
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "mqtt_client.h"

/* ============================================================================
 * CONSTANTS
 * ========================================================================== */

static constexpr size_t MQTT_URI_MAX_LEN     = 128;
static constexpr size_t MQTT_USERNAME_MAX_LEN = 64;
static constexpr size_t MQTT_PASSWORD_MAX_LEN = 64;
static constexpr size_t DEVICE_ID_MAX_LEN    = 48;
static constexpr size_t TOPIC_ROOT_MAX_LEN   = 32;
static constexpr size_t SETUP_POP_MAX_LEN    = 32;
static constexpr size_t SERVICE_NAME_MAX_LEN = 29;

static constexpr const char *NVS_NAMESPACE_NETWORK = "network";
static constexpr const char *NVS_KEY_MQTT_URI       = "mqtt_uri";
static constexpr const char *NVS_KEY_MQTT_USER      = "mqtt_user";
static constexpr const char *NVS_KEY_MQTT_PASS      = "mqtt_pass";
static constexpr const char *NVS_KEY_DEVICE_ID      = "device_id";
static constexpr const char *NVS_KEY_SETUP_POP      = "setup_pop";
static constexpr const char *DEFAULT_TOPIC_ROOT     = "sunsense";
static constexpr const char *MQTT_CONFIG_ENDPOINT   = "mqtt-config";

/* ============================================================================
 * TYPES
 * ========================================================================== */

typedef struct {
    char cmd_cover[96];
    char cmd_mode[96];
    char cmd_position[96];
    char cmd_slat[96];
    char cmd_system[96];
    char state_cover[96];
    char state_mode[96];
    char state_position[96];
    char state_light_raw[96];
    char state_light_filtered[96];
    char state_light_state[96];
    char state_motor[96];
    char state_slat[96];
    char state_slat_position[96];
    char state_health[96];
    char state_network_rssi[96];
    char state_network_online[96];
} TopicConfig_t;

typedef struct {
    char mqtt_broker_uri[MQTT_URI_MAX_LEN + 1];
    char mqtt_username[MQTT_USERNAME_MAX_LEN + 1];
    char mqtt_password[MQTT_PASSWORD_MAX_LEN + 1];
    char device_id[DEVICE_ID_MAX_LEN + 1];
    char topic_root[TOPIC_ROOT_MAX_LEN + 1];
    char setup_pop[SETUP_POP_MAX_LEN + 1];
} NetworkConfig_t;

/* ============================================================================
 * SHARED STATE (defined in network.cpp)
 * ========================================================================== */

extern TopicConfig_t            topics;
extern NetworkConfig_t          network_config;
extern bool                     network_ready;
extern bool                     wifi_connected;
extern bool                     mqtt_connected;
extern esp_mqtt_client_handle_t mqtt_client;

/* ============================================================================
 * FUNCTIONS
 * ========================================================================== */

void      initialize_network(void);
bool      network_is_provisioning_active(void);
bool      network_is_reconnecting(void);
esp_err_t save_nvs_string(const char *key, const char *value);
esp_err_t clear_network_config_from_nvs(void);
