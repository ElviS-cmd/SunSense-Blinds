/**
 * @file main.cpp
 * @brief SunSense V2 Main Application
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "mqtt_client.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "protocomm_ble.h"
#include "protocomm_security.h"
#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_ble.h"
#include "gpio_config.h"

extern "C" {
    #include "button_controller.h"
    #include "mode_controller.h"
    #include "motor_controller.h"
    #include "led_controller.h"
    #include "ldr_controller.h"
    #include "encoder_controller.h"
    #include "servo_controller.h"
    #include "microphone_controller.h"
}

static const char *TAG = "SunSense";

static ButtonController_t button = {};
static ModeController_t mode = {};
static MotorController_t motor = {};
static LEDController_t led = {};
static LDRController_t ldr = {};
static EncoderController_t encoder = {};
static ServoController_t servo = {};
static MicrophoneController_t microphone = {};

static TaskHandle_t task_button = NULL;
static TaskHandle_t task_mode = NULL;
static TaskHandle_t task_motor = NULL;
static TaskHandle_t task_led = NULL;
static TaskHandle_t task_ldr = NULL;
static TaskHandle_t task_encoder = NULL;
static TaskHandle_t task_microphone = NULL;
static SemaphoreHandle_t state_mutex = NULL;
static esp_mqtt_client_handle_t mqtt_client = NULL;
static bool network_ready = false;
static bool wifi_connected = false;
static bool mqtt_connected = false;
static bool network_provisioned = false;
static bool provisioning_active = false;
static uint8_t wifi_retry_count = 0;
static constexpr uint8_t WIFI_MAX_RETRIES = 10;

static constexpr size_t MQTT_URI_MAX_LEN = 128;
static constexpr size_t DEVICE_ID_MAX_LEN = 48;
static constexpr size_t TOPIC_ROOT_MAX_LEN = 32;
static constexpr size_t SETUP_POP_MAX_LEN = 32;
static constexpr size_t SERVICE_NAME_MAX_LEN = 29;
static constexpr const char *NVS_NAMESPACE_NETWORK = "network";
static constexpr const char *NVS_KEY_MQTT_URI = "mqtt_uri";
static constexpr const char *NVS_KEY_DEVICE_ID = "device_id";
static constexpr const char *NVS_KEY_SETUP_POP = "setup_pop";
static constexpr const char *DEFAULT_TOPIC_ROOT = "sunsense";
static constexpr const char *MQTT_CONFIG_ENDPOINT = "mqtt-config";

typedef struct {
    char cmd_cover[96];
    char cmd_mode[96];
    char cmd_position[96];
    char cmd_system[96];
    char state_cover[96];
    char state_mode[96];
    char state_position[96];
    char state_light_raw[96];
    char state_light_filtered[96];
    char state_light_state[96];
    char state_motor[96];
    char state_slat[96];
    char state_health[96];
    char state_network_rssi[96];
    char state_network_online[96];
} TopicConfig_t;

typedef struct {
    char mqtt_broker_uri[MQTT_URI_MAX_LEN + 1];
    char device_id[DEVICE_ID_MAX_LEN + 1];
    char topic_root[TOPIC_ROOT_MAX_LEN + 1];
    char setup_pop[SETUP_POP_MAX_LEN + 1];
} NetworkConfig_t;

typedef struct {
    uint32_t uptime_ms;
    OperatingMode_t current_mode;
    MotorState_t motor_state;
    LightLevel_t light_level;
    bool system_healthy;
} SystemState_t;

typedef struct {
    SystemState_t system;
    uint16_t ldr_raw;
    uint16_t ldr_filtered;
    float encoder_percent;
    float servo_angle;
} PublishSnapshot_t;

static SystemState_t system_state = {};
static SystemHealth_t system_health = {};
static SystemConfig_t system_config = {};
static bool auto_command_pending = false;
static bool manual_next_open = true;
static TopicConfig_t topics = {};
static NetworkConfig_t network_config = {};

static bool lock_state(void) {
    return (xSemaphoreTake(state_mutex, portMAX_DELAY) == pdTRUE);
}

static void unlock_state(void) {
    xSemaphoreGive(state_mutex);
}

static void stop_motor_locked(uint32_t current_time);
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);

static void set_default_network_config(void) {
    uint8_t mac[6] = {};

    memset(&network_config, 0, sizeof(network_config));
    strncpy(network_config.topic_root, DEFAULT_TOPIC_ROOT, sizeof(network_config.topic_root) - 1U);

    if (esp_efuse_mac_get_default(mac) == ESP_OK) {
        snprintf(network_config.device_id,
                 sizeof(network_config.device_id),
                 "device_%02x%02x%02x",
                 mac[3],
                 mac[4],
                 mac[5]);
    } else {
        strncpy(network_config.device_id, "device_unknown", sizeof(network_config.device_id) - 1U);
    }
}

static void ensure_network_identity_defaults(void) {
    if (network_config.topic_root[0] == '\0') {
        strncpy(network_config.topic_root, DEFAULT_TOPIC_ROOT, sizeof(network_config.topic_root) - 1U);
    }

    if (network_config.device_id[0] == '\0') {
        uint8_t mac[6] = {};
        if (esp_efuse_mac_get_default(mac) == ESP_OK) {
            snprintf(network_config.device_id,
                     sizeof(network_config.device_id),
                     "device_%02x%02x%02x",
                     mac[3],
                     mac[4],
                     mac[5]);
        } else {
            strncpy(network_config.device_id, "device_unknown", sizeof(network_config.device_id) - 1U);
        }
    }
}

static esp_err_t load_nvs_string(nvs_handle_t handle,
                                 const char *key,
                                 char *buffer,
                                 size_t buffer_size) {
    size_t required_size = buffer_size;
    esp_err_t err = nvs_get_str(handle, key, buffer, &required_size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        buffer[0] = '\0';
    }
    return err;
}

static esp_err_t save_nvs_string(const char *key, const char *value) {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(NVS_NAMESPACE_NETWORK, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_str(handle, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

static esp_err_t load_network_config_from_nvs(void) {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(NVS_NAMESPACE_NETWORK, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_ERR_NOT_FOUND;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open network NVS namespace: %s", esp_err_to_name(err));
        return err;
    }

    load_nvs_string(handle, NVS_KEY_MQTT_URI, network_config.mqtt_broker_uri, sizeof(network_config.mqtt_broker_uri));
    load_nvs_string(handle, NVS_KEY_DEVICE_ID, network_config.device_id, sizeof(network_config.device_id));
    load_nvs_string(handle, NVS_KEY_SETUP_POP, network_config.setup_pop, sizeof(network_config.setup_pop));
    nvs_close(handle);

    ensure_network_identity_defaults();
    return (network_config.mqtt_broker_uri[0] != '\0') ? ESP_OK : ESP_ERR_NOT_FOUND;
}

static esp_err_t clear_network_config_from_nvs(void) {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(NVS_NAMESPACE_NETWORK, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    nvs_erase_key(handle, NVS_KEY_MQTT_URI);
    nvs_commit(handle);
    nvs_close(handle);
    return ESP_OK;
}

static void ensure_setup_pop(void) {
    if (network_config.setup_pop[0] != '\0') {
        return;
    }

    uint8_t mac[6] = {};
    uint32_t random_part = esp_random();
    if (esp_efuse_mac_get_default(mac) == ESP_OK) {
        snprintf(network_config.setup_pop,
                 sizeof(network_config.setup_pop),
                 "%02X%02X%02X%08lX",
                 mac[3],
                 mac[4],
                 mac[5],
                 static_cast<unsigned long>(random_part));
    } else {
        snprintf(network_config.setup_pop,
                 sizeof(network_config.setup_pop),
                 "%08lX%08lX",
                 static_cast<unsigned long>(esp_random()),
                 static_cast<unsigned long>(random_part));
    }

    if (save_nvs_string(NVS_KEY_SETUP_POP, network_config.setup_pop) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to persist setup PoP");
    }
}

static void get_provisioning_service_name(char *service_name, size_t max_len) {
    snprintf(service_name, max_len, "SunSense-%s", network_config.device_id);
}

static void build_topic(char *buffer, size_t buffer_size, const char *suffix) {
    snprintf(buffer, buffer_size, "%s/%s/%s", network_config.topic_root, network_config.device_id, suffix);
}

static void initialize_topics(void) {
    build_topic(topics.cmd_cover, sizeof(topics.cmd_cover), "cmd/cover");
    build_topic(topics.cmd_mode, sizeof(topics.cmd_mode), "cmd/mode");
    build_topic(topics.cmd_position, sizeof(topics.cmd_position), "cmd/position");
    build_topic(topics.cmd_system, sizeof(topics.cmd_system), "cmd/system");
    build_topic(topics.state_cover, sizeof(topics.state_cover), "state/cover");
    build_topic(topics.state_mode, sizeof(topics.state_mode), "state/mode");
    build_topic(topics.state_position, sizeof(topics.state_position), "state/position");
    build_topic(topics.state_light_raw, sizeof(topics.state_light_raw), "state/light/raw");
    build_topic(topics.state_light_filtered, sizeof(topics.state_light_filtered), "state/light/filtered");
    build_topic(topics.state_light_state, sizeof(topics.state_light_state), "state/light/state");
    build_topic(topics.state_motor, sizeof(topics.state_motor), "state/motor");
    build_topic(topics.state_slat, sizeof(topics.state_slat), "state/slat");
    build_topic(topics.state_health, sizeof(topics.state_health), "state/health");
    build_topic(topics.state_network_rssi, sizeof(topics.state_network_rssi), "state/network/rssi");
    build_topic(topics.state_network_online, sizeof(topics.state_network_online), "state/network/online");
}

static const char *cover_state_to_string(MotorState_t motor_state, float position_percent) {
    if (motor_state == MOTOR_OPENING) {
        return "opening";
    }
    if (motor_state == MOTOR_CLOSING) {
        return "closing";
    }
    if (position_percent <= 1.0f) {
        return "closed";
    }
    if (position_percent >= 99.0f) {
        return "open";
    }
    return "stopped";
}

static const char *slat_state_to_string(float angle) {
    if (angle <= 5.0f) {
        return "closed";
    }
    if (angle >= 85.0f) {
        return "open";
    }
    return "moving";
}

static void apply_manual_mode_locked(uint32_t current_time) {
    if (mode_get_current(&mode) != MODE_MANUAL) {
        mode_cycle_next(&mode, current_time);
        system_state.current_mode = mode_get_current(&mode);
    }
    mode_note_activity(&mode, current_time);
}

static void collect_publish_snapshot_locked(PublishSnapshot_t *snapshot) {
    snapshot->system = system_state;
    snapshot->ldr_raw = system_health.ldr_ok ? ldr_get_raw(&ldr) : 0;
    snapshot->ldr_filtered = system_health.ldr_ok ? ldr_get_filtered(&ldr) : 0;
    snapshot->encoder_percent = system_health.encoder_ok ? encoder_get_percent(&encoder) : 0.0f;
    snapshot->servo_angle = system_health.servo_ok ? servo_get_angle(&servo) : 0.0f;
}

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

static void publish_state_snapshot(const PublishSnapshot_t *snapshot) {
    wifi_ap_record_t ap_info = {};
    bool have_rssi = wifi_connected && (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK);

    mqtt_publish_string(topics.state_mode, mode_to_string(snapshot->system.current_mode));
    mqtt_publish_string(topics.state_cover, cover_state_to_string(snapshot->system.motor_state, snapshot->encoder_percent));
    mqtt_publish_float(topics.state_position, snapshot->encoder_percent);
    mqtt_publish_int(topics.state_light_raw, snapshot->ldr_raw);
    mqtt_publish_int(topics.state_light_filtered, snapshot->ldr_filtered);
    mqtt_publish_string(topics.state_light_state, light_level_to_string(snapshot->system.light_level));
    mqtt_publish_string(topics.state_motor, motor_state_to_string(snapshot->system.motor_state));
    mqtt_publish_string(topics.state_slat, slat_state_to_string(snapshot->servo_angle));
    mqtt_publish_string(topics.state_health, snapshot->system.system_healthy ? "ok" : "fault");
    mqtt_publish_string(topics.state_network_online, wifi_connected ? "true" : "false");
    if (have_rssi) {
        mqtt_publish_int(topics.state_network_rssi, ap_info.rssi);
    }
}

static void publish_state_if_ready(void) {
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
        motor_set_opening(&motor, current_time);
        manual_next_open = false;
    } else if ((len == 5) && (strncmp(payload, "CLOSE", 5) == 0)) {
        motor_set_closing(&motor, current_time);
        manual_next_open = true;
    } else if ((len == 4) && (strncmp(payload, "STOP", 4) == 0)) {
        stop_motor_locked(current_time);
    } else {
        ESP_LOGW(TAG, "Ignoring unknown cover command");
    }

    system_state.current_mode = mode_get_current(&mode);
    system_state.motor_state = motor_get_state(&motor);
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

    float current_position = system_health.encoder_ok ? encoder_get_percent(&encoder) : 0.0f;
    if (requested_position > (int) (current_position + 1.0f)) {
        motor_set_opening(&motor, current_time);
        manual_next_open = false;
    } else if (requested_position < (int) (current_position - 1.0f)) {
        motor_set_closing(&motor, current_time);
        manual_next_open = true;
    } else {
        stop_motor_locked(current_time);
    }

    system_state.current_mode = mode_get_current(&mode);
    system_state.motor_state = motor_get_state(&motor);
    unlock_state();
}

static void handle_system_command(const char *payload, int len) {
    if ((len == 7) && (strncmp(payload, "GO_HOME", 7) == 0)) {
        uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        if (lock_state()) {
            apply_manual_mode_locked(current_time);
            servo_close(&servo, current_time);
            motor_set_closing(&motor, current_time);
            manual_next_open = true;
            system_state.current_mode = mode_get_current(&mode);
            system_state.motor_state = motor_get_state(&motor);
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
    } else {
        ESP_LOGW(TAG, "Ignoring unknown system command");
    }
}

static void handle_mqtt_data_event(const esp_mqtt_event_t *event) {
    if (topic_matches(topics.cmd_cover, event->topic, event->topic_len)) {
        handle_cover_command(event->data, event->data_len);
    } else if (topic_matches(topics.cmd_mode, event->topic, event->topic_len)) {
        handle_mode_command(event->data, event->data_len);
    } else if (topic_matches(topics.cmd_position, event->topic, event->topic_len)) {
        handle_position_command(event->data, event->data_len);
    } else if (topic_matches(topics.cmd_system, event->topic, event->topic_len)) {
        handle_system_command(event->data, event->data_len);
    }

    publish_state_if_ready();
}

static esp_err_t initialize_mqtt_client(void) {
    if (network_config.mqtt_broker_uri[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }

    if (mqtt_client != NULL) {
        return ESP_OK;
    }

    esp_mqtt_client_config_t mqtt_config = {};
    mqtt_config.broker.address.uri = network_config.mqtt_broker_uri;
    mqtt_config.credentials.client_id = network_config.device_id;

    mqtt_client = esp_mqtt_client_init(&mqtt_config);
    if (mqtt_client == NULL) {
        ESP_LOGE(TAG, "Failed to create MQTT client");
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK(esp_mqtt_client_register_event(mqtt_client, MQTT_EVENT_ANY, mqtt_event_handler, NULL));
    network_ready = true;
    return ESP_OK;
}

static void start_wifi_station(void) {
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
}

static esp_err_t mqtt_config_endpoint_handler(uint32_t session_id,
                                              const uint8_t *inbuf,
                                              ssize_t inlen,
                                              uint8_t **outbuf,
                                              ssize_t *outlen,
                                              void *priv_data) {
    (void) session_id;
    (void) priv_data;

    if (inbuf == NULL || inlen <= 0 || inlen > MQTT_URI_MAX_LEN) {
        return ESP_ERR_INVALID_ARG;
    }

    char mqtt_uri[MQTT_URI_MAX_LEN + 1] = {};
    memcpy(mqtt_uri, inbuf, inlen);

    if (strncmp(mqtt_uri, "mqtt://", 7) != 0 && strncmp(mqtt_uri, "mqtts://", 8) != 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = save_nvs_string(NVS_KEY_MQTT_URI, mqtt_uri);
    if (err != ESP_OK) {
        return err;
    }

    strncpy(network_config.mqtt_broker_uri, mqtt_uri, sizeof(network_config.mqtt_broker_uri) - 1U);
    const char response[] = "SUCCESS";
    *outbuf = reinterpret_cast<uint8_t *>(strdup(response));
    if (*outbuf == NULL) {
        return ESP_ERR_NO_MEM;
    }
    *outlen = sizeof(response);
    return ESP_OK;
}

static void mqtt_event_handler(void *handler_args,
                               esp_event_base_t base,
                               int32_t event_id,
                               void *event_data) {
    (void) handler_args;
    (void) base;

    esp_mqtt_event_handle_t event = static_cast<esp_mqtt_event_handle_t>(event_data);
    switch ((esp_mqtt_event_id_t) event_id) {
        case MQTT_EVENT_CONNECTED:
            mqtt_connected = true;
            esp_mqtt_client_subscribe(mqtt_client, topics.cmd_cover, 1);
            esp_mqtt_client_subscribe(mqtt_client, topics.cmd_mode, 1);
            esp_mqtt_client_subscribe(mqtt_client, topics.cmd_position, 1);
            esp_mqtt_client_subscribe(mqtt_client, topics.cmd_system, 1);
            publish_state_if_ready();
            break;
        case MQTT_EVENT_DISCONNECTED:
            mqtt_connected = false;
            break;
        case MQTT_EVENT_DATA:
            handle_mqtt_data_event(event);
            break;
        default:
            break;
    }
}

static void network_event_handler(void *arg,
                                  esp_event_base_t event_base,
                                  int32_t event_id,
                                  void *event_data) {
    (void) arg;
    
    if (event_base == WIFI_PROV_EVENT) {
        switch (event_id) {
            case WIFI_PROV_START:
                ESP_LOGI(TAG, "BLE provisioning started");
                break;
            case WIFI_PROV_CRED_RECV: {
                wifi_sta_config_t *wifi_sta_cfg = static_cast<wifi_sta_config_t *>(event_data);
                ESP_LOGI(TAG, "Received Wi-Fi SSID: %s", reinterpret_cast<const char *>(wifi_sta_cfg->ssid));
                break;
            }
            case WIFI_PROV_CRED_SUCCESS:
                ESP_LOGI(TAG, "Provisioning successful");
                break;
            case WIFI_PROV_CRED_FAIL:
                ESP_LOGE(TAG, "Provisioning failed: bad Wi-Fi credentials, resetting for retry");
                wifi_prov_mgr_reset_sm_state_on_failure();
                break;
            case WIFI_PROV_END:
                provisioning_active = false;
                wifi_prov_mgr_deinit();
                ESP_LOGI(TAG, "Provisioning finished, restarting into normal runtime");
                esp_restart();
                break;
            default:
                break;
        }
    } else if (event_base == PROTOCOMM_TRANSPORT_BLE_EVENT) {
        if (event_id == PROTOCOMM_TRANSPORT_BLE_CONNECTED) {
            ESP_LOGI(TAG, "Provisioning client connected over BLE");
        } else if (event_id == PROTOCOMM_TRANSPORT_BLE_DISCONNECTED) {
            ESP_LOGI(TAG, "Provisioning client disconnected from BLE");
        }
    } else if (event_base == PROTOCOMM_SECURITY_SESSION_EVENT) {
        if (event_id == PROTOCOMM_SECURITY_SESSION_SETUP_OK) {
            ESP_LOGI(TAG, "Provisioning secure session established");
        } else if (event_id == PROTOCOMM_SECURITY_SESSION_CREDENTIALS_MISMATCH) {
            ESP_LOGE(TAG, "Provisioning failed: incorrect setup code");
        }
    } else if ((event_base == WIFI_EVENT) && (event_id == WIFI_EVENT_STA_START)) {
        wifi_retry_count = 0;
        esp_wifi_connect();
    } else if ((event_base == WIFI_EVENT) && (event_id == WIFI_EVENT_STA_DISCONNECTED)) {
        wifi_connected = false;
        mqtt_connected = false;
        if (wifi_retry_count < WIFI_MAX_RETRIES) {
            wifi_retry_count++;
            vTaskDelay(pdMS_TO_TICKS(1000 * wifi_retry_count));
            ESP_LOGW(TAG, "Wi-Fi disconnected, retry %u/%u", wifi_retry_count, WIFI_MAX_RETRIES);
            esp_wifi_connect();
        } else {
            ESP_LOGE(TAG, "Wi-Fi failed after %u retries, giving up", WIFI_MAX_RETRIES);
        }
    } else if ((event_base == IP_EVENT) && (event_id == IP_EVENT_STA_GOT_IP)) {
        wifi_retry_count = 0;
        wifi_connected = true;
        if (mqtt_client == NULL && network_config.mqtt_broker_uri[0] != '\0') {
            ESP_ERROR_CHECK_WITHOUT_ABORT(initialize_mqtt_client());
        }
        if (mqtt_client != NULL) {
            esp_mqtt_client_start(mqtt_client);
        }
    }
}

static void initialize_network_stack(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, &network_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(PROTOCOMM_TRANSPORT_BLE_EVENT, ESP_EVENT_ANY_ID, &network_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(PROTOCOMM_SECURITY_SESSION_EVENT, ESP_EVENT_ANY_ID, &network_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &network_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &network_event_handler, NULL));

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t wifi_init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init));
}

static void initialize_network(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    set_default_network_config();
    load_network_config_from_nvs();
    ensure_setup_pop();
    initialize_topics();
    initialize_network_stack();

    wifi_prov_mgr_config_t prov_config = {};
    prov_config.scheme = wifi_prov_scheme_ble;
    prov_config.scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM;
    prov_config.app_event_handler = WIFI_PROV_EVENT_HANDLER_NONE;
    ESP_ERROR_CHECK(wifi_prov_mgr_init(prov_config));

    bool wifi_provisioned = false;
    ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&wifi_provisioned));
    network_provisioned = wifi_provisioned && (network_config.mqtt_broker_uri[0] != '\0');

    if (!network_provisioned) {
        char service_name[SERVICE_NAME_MAX_LEN + 1] = {};
        get_provisioning_service_name(service_name, sizeof(service_name));

        provisioning_active = true;
        wifi_prov_mgr_endpoint_create(MQTT_CONFIG_ENDPOINT);
        ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(
            WIFI_PROV_SECURITY_1,
            static_cast<const void *>(network_config.setup_pop),
            service_name,
            NULL));
        ESP_ERROR_CHECK(wifi_prov_mgr_endpoint_register(MQTT_CONFIG_ENDPOINT, mqtt_config_endpoint_handler, NULL));

        ESP_LOGI(TAG, "Provision with BLE service name: %s", service_name);
        ESP_LOGI(TAG, "Use setup code: %s", network_config.setup_pop);
        ESP_LOGI(TAG, "Send MQTT broker URI to custom endpoint: %s", MQTT_CONFIG_ENDPOINT);
        return;
    }

    wifi_prov_mgr_deinit();
    ESP_ERROR_CHECK(initialize_mqtt_client());
    start_wifi_station();
}

static bool create_task_checked(TaskFunction_t task_fn,
                                const char *name,
                                uint32_t stack_size,
                                UBaseType_t priority,
                                TaskHandle_t *handle,
                                BaseType_t core_id) {
    BaseType_t result = xTaskCreatePinnedToCore(
        task_fn,
        name,
        stack_size,
        NULL,
        priority,
        handle,
        core_id);

    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create task: %s", name);
        return false;
    }

    return true;
}

static void handle_manual_button_locked(uint32_t current_time) {
    MotorState_t current_motor_state = motor_get_state(&motor);

    if (current_motor_state == MOTOR_OPENING || current_motor_state == MOTOR_CLOSING) {
        motor_stop(&motor, current_time);
        manual_next_open = (current_motor_state == MOTOR_CLOSING);
    } else if (manual_next_open) {
        motor_set_opening(&motor, current_time);
        manual_next_open = false;
    } else {
        motor_set_closing(&motor, current_time);
        manual_next_open = true;
    }

    system_state.motor_state = motor_get_state(&motor);
}

static void stop_motor_locked(uint32_t current_time) {
    if (motor_is_running(&motor)) {
        motor_stop(&motor, current_time);
        system_state.motor_state = motor_get_state(&motor);
    }
}

static void task_button_handler(void *pvParameters) {
    (void)pvParameters;
    ESP_LOGI(TAG, "Button task started");

    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(TASK_PERIOD_BUTTON);

    while (1) {
        uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        button_update(&button, current_time);

        ButtonAction_t action = button_get_action(&button);
        if (action != BUTTON_ACTION_NONE && lock_state()) {
            OperatingMode_t current_mode = mode_get_current(&mode);

            if (action == BUTTON_ACTION_LONG) {
                mode_handle_button(&mode, action, current_time);
                stop_motor_locked(current_time);
                auto_command_pending = system_health.ldr_ok;
                manual_next_open = true;
            } else if (current_mode == MODE_AUTO) {
                mode_handle_button(&mode, action, current_time);
                stop_motor_locked(current_time);
                manual_next_open = true;
            } else {
                mode_note_activity(&mode, current_time);
                handle_manual_button_locked(current_time);
            }

            system_state.current_mode = mode_get_current(&mode);
            button_clear_action(&button);
            unlock_state();
        }

        vTaskDelayUntil(&last_wake_time, period);
    }
}

static void task_mode_handler(void *pvParameters) {
    (void)pvParameters;
    ESP_LOGI(TAG, "Mode task started");

    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(TASK_PERIOD_MODE);

    while (1) {
        uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        bool changed = false;
        OperatingMode_t current_mode = MODE_AUTO;

        if (lock_state()) {
            mode_update_idle(&mode, current_time);
            changed = mode_changed(&mode);
            current_mode = mode_get_current(&mode);
            system_state.current_mode = current_mode;

            if (changed && current_mode == MODE_AUTO) {
                stop_motor_locked(current_time);
                auto_command_pending = system_health.ldr_ok;
                manual_next_open = true;
            }

            unlock_state();
        }

        if (changed) {
            ESP_LOGI(TAG, "Mode changed to: %s", mode_to_string(current_mode));
        }

        vTaskDelayUntil(&last_wake_time, period);
    }
}

static void task_motor_handler(void *pvParameters) {
    (void)pvParameters;
    ESP_LOGI(TAG, "Motor task started");

    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(TASK_PERIOD_MOTOR);

    while (1) {
        uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;

        if (lock_state()) {
            OperatingMode_t current_mode = mode_get_current(&mode);
            MotorState_t motor_state = motor_get_state(&motor);

            if (current_mode == MODE_AUTO) {
                if (motor_state != MOTOR_STOP) {
                    bool timeout_reached = (motor_get_elapsed_time(&motor, current_time) >= system_config.motor_timeout_ms);
                    bool endpoint_reached = false;

                    if (system_health.encoder_ok && encoder_is_healthy(&encoder)) {
                        float position_percent = encoder_get_percent(&encoder);
                        endpoint_reached =
                            ((motor_state == MOTOR_OPENING) && (position_percent >= 99.0f)) ||
                            ((motor_state == MOTOR_CLOSING) && (position_percent <= 1.0f));
                    }

                    if (timeout_reached || endpoint_reached) {
                        stop_motor_locked(current_time);
                    }
                } else if (auto_command_pending && system_health.ldr_ok) {
                    if (ldr_is_bright(&ldr)) {
                        motor_set_opening(&motor, current_time);
                    } else if (ldr_is_dark(&ldr)) {
                        motor_set_closing(&motor, current_time);
                    }
                    auto_command_pending = false;
                }
            }

            system_state.motor_state = motor_get_state(&motor);
            unlock_state();
        }

        vTaskDelayUntil(&last_wake_time, period);
    }
}

static void task_led_handler(void *pvParameters) {
    (void)pvParameters;
    ESP_LOGI(TAG, "LED task started");

    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(TASK_PERIOD_LED);

    while (1) {
        uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        bool healthy = false;
        OperatingMode_t current_mode = MODE_AUTO;

        if (lock_state()) {
            healthy = system_state.system_healthy;
            current_mode = system_state.current_mode;
            unlock_state();
        }

        if (system_health.led_ok) {
            led_update(&led, current_time);
            led_set_green(&led, healthy ? LED_BLINK_SLOW : LED_BLINK_FAST);
            led_set_blue(&led, (current_mode == MODE_AUTO) ? LED_ON : LED_BLINK_SLOW);
        }

        vTaskDelayUntil(&last_wake_time, period);
    }
}

static void task_ldr_handler(void *pvParameters) {
    (void)pvParameters;
    ESP_LOGI(TAG, "LDR task started");

    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(TASK_PERIOD_LDR);

    while (1) {
        uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        bool level_changed = false;
        LightLevel_t light_level = LIGHT_DARK;
        uint16_t raw_level = 0;

        if (system_health.ldr_ok && lock_state()) {
            ldr_update(&ldr, current_time);
            level_changed = ldr_level_changed(&ldr);
            light_level = ldr_get_level(&ldr);
            raw_level = ldr_get_raw(&ldr);
            system_state.light_level = light_level;

            if (level_changed) {
                auto_command_pending = (mode_get_current(&mode) == MODE_AUTO);
            }

            unlock_state();
        }

        if (level_changed) {
            ESP_LOGI(TAG, "Light level: %s (raw: %u)", light_level_to_string(light_level), raw_level);
        }

        vTaskDelayUntil(&last_wake_time, period);
    }
}

static void task_encoder_handler(void *pvParameters) {
    (void)pvParameters;
    ESP_LOGI(TAG, "Encoder task started");

    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(TASK_PERIOD_ENCODER);
    uint32_t last_log = 0;

    while (1) {
        uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        bool updated = false;
        float angle = 0.0f;

        if (system_health.encoder_ok && lock_state()) {
            updated = encoder_update(&encoder, current_time);
            angle = encoder_get_degrees(&encoder);
            unlock_state();
        }

        if (updated && (current_time - last_log > 1000U)) {
            ESP_LOGI(TAG, "Encoder: %.1f°", angle);
            last_log = current_time;
        }

        vTaskDelayUntil(&last_wake_time, period);
    }
}

static void task_microphone_handler(void *pvParameters) {
    (void)pvParameters;
    ESP_LOGI(TAG, "Microphone task started");

    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(TASK_PERIOD_MICROPHONE);
    uint32_t last_log = 0;

    while (1) {
        uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;

        if (system_health.microphone_ok) {
            microphone_update(&microphone, current_time);

            if (microphone_is_buffer_ready(&microphone)) {
                uint16_t level = microphone_get_level(&microphone);

                if (current_time - last_log > 2000U) {
                    ESP_LOGI(TAG, "Audio level: %u", level);
                    last_log = current_time;
                }

                microphone_clear_buffer(&microphone);
            }
        }

        vTaskDelayUntil(&last_wake_time, period);
    }
}

static bool initialize_all_controllers(void) {
    ESP_LOGI(TAG, "Initializing controllers...");

    system_config = get_default_config();

    system_health.button_ok = button_init(&button);
    mode_init(&mode);
    system_health.motor_ok = motor_init(&motor);
    system_health.led_ok = led_init(&led);
    system_health.ldr_ok = ldr_init(&ldr);
    system_health.encoder_ok = encoder_init(&encoder);
    system_health.servo_ok = servo_init(&servo);
    system_health.microphone_ok = microphone_init(&microphone);

    system_state.current_mode = mode_get_current(&mode);
    system_state.motor_state = motor_get_state(&motor);
    system_state.light_level = ldr_get_level(&ldr);
    system_state.system_healthy =
        system_health.button_ok &&
        system_health.motor_ok &&
        system_health.led_ok &&
        system_health.ldr_ok &&
        system_health.encoder_ok &&
        system_health.servo_ok &&
        system_health.microphone_ok;

    auto_command_pending = system_health.ldr_ok;
    manual_next_open = true;

    ESP_LOGI(TAG, "Button: %s", system_health.button_ok ? "OK" : "FAILED");
    ESP_LOGI(TAG, "Motor: %s", system_health.motor_ok ? "OK" : "FAILED");
    ESP_LOGI(TAG, "LED: %s", system_health.led_ok ? "OK" : "FAILED");
    ESP_LOGI(TAG, "LDR: %s", system_health.ldr_ok ? "OK" : "FAILED");
    ESP_LOGI(TAG, "Encoder: %s", system_health.encoder_ok ? "OK" : "FAILED");
    ESP_LOGI(TAG, "Servo: %s", system_health.servo_ok ? "OK" : "FAILED");
    ESP_LOGI(TAG, "Microphone: %s", system_health.microphone_ok ? "OK" : "FAILED");

    return system_state.system_healthy;
}

static bool create_all_tasks(void) {
    ESP_LOGI(TAG, "Creating FreeRTOS tasks...");

    return
        create_task_checked(task_button_handler, "button_task", TASK_STACK_BUTTON, TASK_PRIORITY_BUTTON, &task_button, 0) &&
        create_task_checked(task_mode_handler, "mode_task", TASK_STACK_MODE, TASK_PRIORITY_MODE, &task_mode, 0) &&
        create_task_checked(task_motor_handler, "motor_task", TASK_STACK_MOTOR, TASK_PRIORITY_MOTOR, &task_motor, 1) &&
        create_task_checked(task_led_handler, "led_task", TASK_STACK_LED, TASK_PRIORITY_LED, &task_led, 0) &&
        create_task_checked(task_ldr_handler, "ldr_task", TASK_STACK_LDR, TASK_PRIORITY_LDR, &task_ldr, 0) &&
        create_task_checked(task_encoder_handler, "encoder_task", TASK_STACK_ENCODER, TASK_PRIORITY_ENCODER, &task_encoder, 0) &&
        create_task_checked(task_microphone_handler, "microphone_task", TASK_STACK_MICROPHONE, TASK_PRIORITY_MICROPHONE, &task_microphone, 1);
}

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Starting SunSense V2");

    state_mutex = xSemaphoreCreateMutex();
    if (state_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create state mutex");
        return;
    }

    if (!initialize_all_controllers()) {
        ESP_LOGE(TAG, "Controller initialization failed, refusing to start tasks");
        return;
    }

    if (!create_all_tasks()) {
        ESP_LOGE(TAG, "Task creation failed, refusing to continue");
        return;
    }

    initialize_network();

    uint32_t last_log_time = 0;
    uint32_t last_publish_time = 0;

    while (1) {
        uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        SystemState_t snapshot = {};

        if (lock_state()) {
            servo_update(&servo, current_time);
            system_state.uptime_ms = current_time;
            snapshot = system_state;
            unlock_state();
        }

        if (current_time - last_log_time > 10000U) {
            ESP_LOGI(TAG, "=== System Status ===");
            ESP_LOGI(TAG, "Uptime: %lu ms", static_cast<unsigned long>(snapshot.uptime_ms));
            ESP_LOGI(TAG, "Mode: %s", mode_to_string(snapshot.current_mode));
            ESP_LOGI(TAG, "Motor: %s", motor_state_to_string(snapshot.motor_state));
            ESP_LOGI(TAG, "Light: %s", light_level_to_string(snapshot.light_level));
            ESP_LOGI(TAG, "Healthy: %s", snapshot.system_healthy ? "YES" : "NO");
            last_log_time = current_time;
        }

        if (current_time - last_publish_time > 5000U) {
            publish_state_if_ready();
            last_publish_time = current_time;
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
