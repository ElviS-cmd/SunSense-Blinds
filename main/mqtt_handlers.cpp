/**
 * @file mqtt_handlers.cpp
 * @brief MQTT command handlers and state publishing
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "gpio_config.h"
#include "mqtt_client.h"
#include "wifi_provisioning/manager.h"
#include "mqtt_handlers.h"
#include "network.h"

static const char *TAG = "MQTT";

/* ============================================================================
 * STATE STRING HELPERS
 * ========================================================================== */

static const char *cover_state_to_string(MotorState_t motor_state, float position_percent) {
    if (motor_state == MOTOR_OPENING) return "opening";
    if (motor_state == MOTOR_CLOSING) return "closing";
    if (position_percent <= 1.0f)    return "closed";
    if (position_percent >= 99.0f)   return "open";
    return "stopped";
}

static const char *slat_state_to_string(float angle) {
    if (angle <= SERVO_SLAT_CLOSED_ANGLE + 1.0f) return "closed";
    if (angle >= SERVO_SLAT_OPEN_ANGLE - 1.0f)   return "open";
    return "moving";
}

static int slat_angle_to_percent(float angle) {
    if (angle <= SERVO_SLAT_CLOSED_ANGLE) {
        return 0;
    }
    if (angle >= SERVO_SLAT_OPEN_ANGLE) {
        return 100;
    }

    float range = SERVO_SLAT_OPEN_ANGLE - SERVO_SLAT_CLOSED_ANGLE;
    if (range <= 0.0f) {
        return 0;
    }
    return (int)(((angle - SERVO_SLAT_CLOSED_ANGLE) * 100.0f / range) + 0.5f);
}

static float slat_percent_to_angle(long percent) {
    if (percent <= 0) {
        return SERVO_SLAT_CLOSED_ANGLE;
    }
    if (percent >= 100) {
        return SERVO_SLAT_OPEN_ANGLE;
    }

    float range = SERVO_SLAT_OPEN_ANGLE - SERVO_SLAT_CLOSED_ANGLE;
    return SERVO_SLAT_CLOSED_ANGLE + ((float)percent * range / 100.0f);
}

/* ============================================================================
 * MQTT PUBLISH HELPERS
 * ========================================================================== */

static void mqtt_publish_string(const char *topic, const char *value) {
    if (!mqtt_connected || mqtt_client == NULL) {
        return;
    }
    esp_mqtt_client_publish(mqtt_client, topic, value, 0, 1, 0);
}

static void mqtt_publish_int(const char *topic, int value) {
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%d", value);
    mqtt_publish_string(topic, buffer);
}

static void mqtt_publish_float(const char *topic, float value) {
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%.1f", value);
    mqtt_publish_string(topic, buffer);
}

static void mqtt_publish_retained(const char *topic, const char *value) {
    if (!mqtt_connected || mqtt_client == NULL) {
        return;
    }
    esp_mqtt_client_publish(mqtt_client, topic, value, 0, 1, 1);
}

static void build_discovery_topic(char *buffer,
                                  size_t buffer_size,
                                  const char *component,
                                  const char *object_id) {
    snprintf(buffer, buffer_size, "homeassistant/%s/%s/%s/config",
             component, network_config.device_id, object_id);
}

static int append_device_info(char *buffer, size_t buffer_size, int offset) {
    return snprintf(buffer + offset, buffer_size - (size_t)offset,
                    "\"device\":{\"identifiers\":[\"sunsense_%s\"],"
                    "\"name\":\"SunSense Blinds %s\","
                    "\"manufacturer\":\"SunSense\","
                    "\"model\":\"ESP32-S3 Blinds Controller\"}",
                    network_config.device_id, network_config.device_id);
}

static void publish_cover_discovery(void) {
    char topic[128] = {};
    char payload[1024] = {};

    build_discovery_topic(topic, sizeof(topic), "cover", "cover");
    int len = snprintf(payload, sizeof(payload),
        "{\"name\":\"Blinds\","
        "\"unique_id\":\"sunsense_%s_cover\","
        "\"command_topic\":\"%s\","
        "\"state_topic\":\"%s\","
        "\"position_topic\":\"%s\","
        "\"set_position_topic\":\"%s\","
        "\"tilt_command_topic\":\"%s\","
        "\"tilt_status_topic\":\"%s\","
        "\"tilt_min\":0,"
        "\"tilt_max\":100,"
        "\"tilt_closed_value\":0,"
        "\"tilt_opened_value\":100,"
        "\"payload_open\":\"OPEN\","
        "\"payload_close\":\"CLOSE\","
        "\"payload_stop\":\"STOP\","
        "\"state_open\":\"open\","
        "\"state_closed\":\"closed\","
        "\"state_opening\":\"opening\","
        "\"state_closing\":\"closing\","
        "\"state_stopped\":\"stopped\","
        "\"position_open\":100,"
        "\"position_closed\":0,"
        "\"availability_topic\":\"%s\","
        "\"payload_available\":\"online\","
        "\"payload_not_available\":\"offline\",",
        network_config.device_id,
        topics.cmd_cover,
        topics.state_cover,
        topics.state_position,
        topics.cmd_position,
        topics.cmd_slat,
        topics.state_slat_position,
        topics.state_network_online);
    len += append_device_info(payload, sizeof(payload), len);
    snprintf(payload + len, sizeof(payload) - (size_t)len, "}");
    mqtt_publish_retained(topic, payload);
}

static void publish_select_discovery(void) {
    char topic[128] = {};
    char payload[768] = {};

    build_discovery_topic(topic, sizeof(topic), "select", "mode");
    int len = snprintf(payload, sizeof(payload),
        "{\"name\":\"Mode\","
        "\"unique_id\":\"sunsense_%s_mode\","
        "\"command_topic\":\"%s\","
        "\"state_topic\":\"%s\","
        "\"options\":[\"AUTO\",\"MANUAL\"],"
        "\"availability_topic\":\"%s\","
        "\"payload_available\":\"online\","
        "\"payload_not_available\":\"offline\",",
        network_config.device_id,
        topics.cmd_mode,
        topics.state_mode,
        topics.state_network_online);
    len += append_device_info(payload, sizeof(payload), len);
    snprintf(payload + len, sizeof(payload) - (size_t)len, "}");
    mqtt_publish_retained(topic, payload);
}

static void publish_sensor_discovery(const char *object_id,
                                     const char *name,
                                     const char *state_topic,
                                     const char *unit,
                                     const char *device_class) {
    char topic[128] = {};
    char payload[768] = {};

    build_discovery_topic(topic, sizeof(topic), "sensor", object_id);
    int len = snprintf(payload, sizeof(payload),
        "{\"name\":\"%s\","
        "\"unique_id\":\"sunsense_%s_%s\","
        "\"state_topic\":\"%s\",",
        name,
        network_config.device_id,
        object_id,
        state_topic);
    if (unit != NULL) {
        len += snprintf(payload + len, sizeof(payload) - (size_t)len,
                        "\"unit_of_measurement\":\"%s\",", unit);
    }
    if (device_class != NULL) {
        len += snprintf(payload + len, sizeof(payload) - (size_t)len,
                        "\"device_class\":\"%s\",", device_class);
    }
    len += snprintf(payload + len, sizeof(payload) - (size_t)len,
                    "\"availability_topic\":\"%s\","
                    "\"payload_available\":\"online\","
                    "\"payload_not_available\":\"offline\",",
                    topics.state_network_online);
    len += append_device_info(payload, sizeof(payload), len);
    snprintf(payload + len, sizeof(payload) - (size_t)len, "}");
    mqtt_publish_retained(topic, payload);
}

static void clear_home_assistant_retained(void) {
    /* Wipe every retained MQTT message this device has ever published so that
     * Home Assistant completely forgets it.  Publishing an empty payload with
     * retain=1 removes the retained message from the broker. */

    static const struct { const char *component; const char *object_id; }
    discovery_entries[] = {
        { "cover",  "cover"          },
        { "select", "mode"           },
        { "sensor", "position"       },
        { "sensor", "light_raw"      },
        { "sensor", "light_filtered" },
        { "sensor", "light_state"    },
        { "sensor", "motor"          },
        { "sensor", "slat"           },
        { "sensor", "slat_position"  },
        { "sensor", "health"         },
        { "sensor", "rssi"           },
    };

    char topic[128] = {};
    for (size_t i = 0; i < sizeof(discovery_entries) / sizeof(discovery_entries[0]); i++) {
        build_discovery_topic(topic, sizeof(topic),
                              discovery_entries[i].component,
                              discovery_entries[i].object_id);
        mqtt_publish_retained(topic, "");
    }

    mqtt_publish_retained(topics.state_cover,           "");
    mqtt_publish_retained(topics.state_mode,            "");
    mqtt_publish_retained(topics.state_position,        "");
    mqtt_publish_retained(topics.state_light_raw,       "");
    mqtt_publish_retained(topics.state_light_filtered,  "");
    mqtt_publish_retained(topics.state_light_state,     "");
    mqtt_publish_retained(topics.state_motor,           "");
    mqtt_publish_retained(topics.state_slat,            "");
    mqtt_publish_retained(topics.state_slat_position,   "");
    mqtt_publish_retained(topics.state_health,          "");
    mqtt_publish_retained(topics.state_network_online,  "");
    mqtt_publish_retained(topics.state_network_rssi,    "");

    ESP_LOGI(TAG, "Cleared all Home Assistant retained messages");
}

static void publish_home_assistant_discovery(void) {
    publish_cover_discovery();
    publish_select_discovery();
    publish_sensor_discovery("position", "Position", topics.state_position, "%", NULL);
    publish_sensor_discovery("light_raw", "Light Raw", topics.state_light_raw, NULL, NULL);
    publish_sensor_discovery("light_filtered", "Light Filtered", topics.state_light_filtered, NULL, NULL);
    publish_sensor_discovery("light_state", "Light State", topics.state_light_state, NULL, NULL);
    publish_sensor_discovery("motor", "Motor", topics.state_motor, NULL, NULL);
    publish_sensor_discovery("slat", "Slat", topics.state_slat, NULL, NULL);
    publish_sensor_discovery("slat_position", "Slat Position", topics.state_slat_position, "%", NULL);
    publish_sensor_discovery("health", "Health", topics.state_health, NULL, NULL);
    publish_sensor_discovery("rssi", "Wi-Fi RSSI", topics.state_network_rssi, "dBm", "signal_strength");
}

/* ============================================================================
 * STATE PUBLISHING
 * ========================================================================== */

static void publish_state_snapshot(const PublishSnapshot_t *snapshot) {
    wifi_ap_record_t ap_info = {};
    bool have_rssi = wifi_connected && (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK);

    mqtt_publish_string(topics.state_mode,         mode_to_string(snapshot->system.current_mode));
    mqtt_publish_string(topics.state_cover,        cover_state_to_string(snapshot->system.motor_state, snapshot->encoder_percent));
    mqtt_publish_float (topics.state_position,     snapshot->encoder_percent);
    mqtt_publish_int   (topics.state_light_raw,    snapshot->ldr_raw);
    mqtt_publish_int   (topics.state_light_filtered, snapshot->ldr_filtered);
    mqtt_publish_string(topics.state_light_state,  light_level_to_string(snapshot->system.light_level));
    mqtt_publish_string(topics.state_motor,        motor_state_to_string(snapshot->system.motor_state));
    mqtt_publish_string(topics.state_slat,         slat_state_to_string(snapshot->servo_angle));
    mqtt_publish_int   (topics.state_slat_position, slat_angle_to_percent(snapshot->servo_angle));
    mqtt_publish_string(topics.state_health,       snapshot->system.system_healthy ? "ok" : "fault");
    mqtt_publish_retained(topics.state_network_online, wifi_connected ? "online" : "offline");
    if (have_rssi) {
        mqtt_publish_int(topics.state_network_rssi, ap_info.rssi);
    }
}

void publish_state_if_ready(void) {
    if (!network_ready || !mqtt_connected) {
        return;
    }

    PublishSnapshot_t snapshot = {};
    if (lock_state()) {
        collect_publish_snapshot_locked(&snapshot);
        unlock_state();
    } else {
        return;
    }

    publish_state_snapshot(&snapshot);
}

/* ============================================================================
 * COMMAND HANDLERS
 * ========================================================================== */

static bool topic_matches(const char *expected, const char *actual, int actual_len) {
    size_t expected_len = strlen(expected);
    return (expected_len == (size_t) actual_len) && (strncmp(expected, actual, expected_len) == 0);
}

static void handle_mode_command(const char *payload, int len) {
    uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;

    if (!lock_state()) {
        return;
    }

    if ((len == 4) && (strncmp(payload, "AUTO", 4) == 0)) {
        mode_return_to_auto(&mode, current_time);
        auto_command_pending = system_health.ldr_ok;
        manual_next_open = true;
    } else if ((len == 6) && (strncmp(payload, "MANUAL", 6) == 0)) {
        apply_manual_mode_locked(current_time);
    } else {
        ESP_LOGW(TAG, "Ignoring unknown mode command");
    }

    system_state.current_mode = mode_get_current(&mode);
    unlock_state();
}

static void handle_cover_command(const char *payload, int len) {
    uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;

    if (!lock_state()) {
        return;
    }

    apply_manual_mode_locked(current_time);

    if ((len == 4) && (strncmp(payload, "OPEN", 4) == 0)) {
        begin_open_sequence_locked(current_time);
        manual_next_open = false;
    } else if ((len == 5) && (strncmp(payload, "CLOSE", 5) == 0)) {
        begin_close_sequence_locked(current_time);
        manual_next_open = true;
    } else if ((len == 4) && (strncmp(payload, "STOP", 4) == 0)) {
        stop_motor_locked(current_time);
    } else {
        ESP_LOGW(TAG, "Ignoring unknown cover command");
    }

    system_state.current_mode = mode_get_current(&mode);
    system_state.motor_state  = motor_get_state(&motor);
    unlock_state();
}

static void handle_position_command(const char *payload, int len) {
    char buffer[8] = {};
    uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;

    if (len <= 0 || len >= (int) sizeof(buffer)) {
        ESP_LOGW(TAG, "Ignoring invalid position command");
        return;
    }

    memcpy(buffer, payload, len);
    char *endptr = NULL;
    long requested_position = strtol(buffer, &endptr, 10);
    if (endptr == buffer || requested_position < 0 || requested_position > 100) {
        ESP_LOGW(TAG, "Ignoring invalid position command");
        return;
    }

    if (!lock_state()) {
        return;
    }

    apply_manual_mode_locked(current_time);

    PublishSnapshot_t snapshot = {};
    collect_publish_snapshot_locked(&snapshot);
    float current_position = snapshot.encoder_percent;
    if ((float)requested_position > current_position + 1.0f) {
        begin_open_sequence_locked(current_time);
        manual_next_open = false;
    } else if ((float)requested_position < current_position - 1.0f) {
        begin_close_sequence_locked(current_time);
        manual_next_open = true;
    } else {
        stop_motor_locked(current_time);
    }

    system_state.current_mode = mode_get_current(&mode);
    system_state.motor_state  = motor_get_state(&motor);
    unlock_state();
}

static void handle_slat_command(const char *payload, int len) {
    char buffer[8] = {};
    uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;

    if (!lock_state()) {
        return;
    }

    apply_manual_mode_locked(current_time);

    if ((len == 4) && (strncmp(payload, "OPEN", 4) == 0)) {
        command_slat_locked(SERVO_SLAT_OPEN_ANGLE, current_time, true, "mqtt slat open");
    } else if ((len == 5) && (strncmp(payload, "CLOSE", 5) == 0)) {
        command_slat_locked(SERVO_SLAT_CLOSED_ANGLE, current_time, true, "mqtt slat close");
    } else if ((len == 4) && (strncmp(payload, "STOP", 4) == 0)) {
        command_slat_locked(servo_get_target(&servo), current_time, true, "mqtt slat stop");
    } else if (len > 0 && len < (int) sizeof(buffer)) {
        memcpy(buffer, payload, len);
        char *endptr = NULL;
        long requested_percent = strtol(buffer, &endptr, 10);
        if (endptr != buffer && requested_percent >= 0 && requested_percent <= 100) {
            command_slat_locked(slat_percent_to_angle(requested_percent), current_time, true, "mqtt slat percent");
        } else {
            ESP_LOGW(TAG, "Ignoring invalid slat command");
        }
    } else {
        ESP_LOGW(TAG, "Ignoring invalid slat command");
    }

    system_state.current_mode = mode_get_current(&mode);
    unlock_state();
}

static void handle_system_command(const char *payload, int len) {
    if ((len == 7) && (strncmp(payload, "GO_HOME", 7) == 0)) {
        uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        if (lock_state()) {
            apply_manual_mode_locked(current_time);
            begin_close_sequence_locked(current_time);
            manual_next_open = true;
            system_state.current_mode = mode_get_current(&mode);
            system_state.motor_state  = motor_get_state(&motor);
            unlock_state();
        }
    } else if ((len == 11) && (strncmp(payload, "REPROVISION", 11) == 0)) {
        if (clear_network_config_from_nvs() == ESP_OK) {
            wifi_prov_mgr_reset_provisioning();
            ESP_LOGW(TAG, "Network settings cleared from NVS; restarting for reprovision");
            esp_restart();
        } else {
            ESP_LOGE(TAG, "Failed to clear network settings for reprovision");
        }

    } else if ((len == 8) && (strncmp(payload, "RESET_HA", 8) == 0)) {
        /* Remove every retained MQTT message from the broker so Home Assistant
         * forgets this device, then re-publish discovery so it registers fresh. */
        ESP_LOGI(TAG, "RESET_HA: wiping retained messages and re-announcing to Home Assistant");
        clear_home_assistant_retained();
        publish_home_assistant_discovery();
        publish_state_if_ready();

    } else if ((len == 10) && (strncmp(payload, "SET_CLOSED", 10) == 0)) {
        /* Calibration: user has physically moved the blinds to the fully-closed
         * position and is telling the firmware to treat it as 0%. */
        ESP_LOGI(TAG, "SET_CLOSED: marking current position as 0%% (fully closed)");
        if (lock_state()) {
            set_position_locked(0U);
            unlock_state();
        }
        publish_state_if_ready();

    } else if ((len == 8) && (strncmp(payload, "SET_OPEN", 8) == 0)) {
        /* Calibration: user has physically moved the blinds to the fully-open
         * position and is telling the firmware to treat it as 100%. */
        ESP_LOGI(TAG, "SET_OPEN: marking current position as 100%% (fully open)");
        if (lock_state()) {
            set_position_locked(100U);
            unlock_state();
        }
        publish_state_if_ready();

    } else {
        ESP_LOGW(TAG, "Ignoring unknown system command");
    }
}

static void handle_mqtt_data_event(const esp_mqtt_event_t *event) {
    if (topic_matches(topics.cmd_cover,    event->topic, event->topic_len)) {
        handle_cover_command(event->data, event->data_len);
    } else if (topic_matches(topics.cmd_mode,  event->topic, event->topic_len)) {
        handle_mode_command(event->data, event->data_len);
    } else if (topic_matches(topics.cmd_position, event->topic, event->topic_len)) {
        handle_position_command(event->data, event->data_len);
    } else if (topic_matches(topics.cmd_slat, event->topic, event->topic_len)) {
        handle_slat_command(event->data, event->data_len);
    } else if (topic_matches(topics.cmd_system,  event->topic, event->topic_len)) {
        handle_system_command(event->data, event->data_len);
    }

    publish_state_if_ready();
}

/* ============================================================================
 * MQTT EVENT HANDLER
 * ========================================================================== */

void mqtt_event_handler(void *handler_args,
                        esp_event_base_t base,
                        int32_t event_id,
                        void *event_data) {
    (void) handler_args;
    (void) base;

    esp_mqtt_event_handle_t event = static_cast<esp_mqtt_event_handle_t>(event_data);
    switch ((esp_mqtt_event_id_t) event_id) {
        case MQTT_EVENT_CONNECTED:
            mqtt_connected = true;
            ESP_LOGI(TAG, "MQTT connected; publishing Home Assistant discovery");
            request_led_status_event(LED_STATUS_NORMAL);
            mqtt_publish_retained(topics.state_network_online, "online");
            publish_home_assistant_discovery();
            esp_mqtt_client_subscribe(mqtt_client, topics.cmd_cover,    1);
            esp_mqtt_client_subscribe(mqtt_client, topics.cmd_mode,     1);
            esp_mqtt_client_subscribe(mqtt_client, topics.cmd_position, 1);
            esp_mqtt_client_subscribe(mqtt_client, topics.cmd_slat,     1);
            esp_mqtt_client_subscribe(mqtt_client, topics.cmd_system,   1);
            publish_state_if_ready();
            break;
        case MQTT_EVENT_DISCONNECTED:
            mqtt_connected = false;
            ESP_LOGW(TAG, "MQTT disconnected");
            request_led_status_event(LED_STATUS_RECONNECTING);
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT connection error");
            request_led_status_event(LED_STATUS_RECONNECTING);
            break;
        case MQTT_EVENT_DATA:
            handle_mqtt_data_event(event);
            break;
        default:
            break;
    }
}
