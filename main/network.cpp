/**
 * @file network.cpp
 * @brief Network configuration, BLE provisioning, WiFi, and MQTT connectivity
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "protocomm_ble.h"
#include "protocomm_security.h"
#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_ble.h"
#include "network.h"
#include "mqtt_handlers.h"

static const char *TAG = "Network";

/* ============================================================================
 * SHARED STATE DEFINITIONS
 * ========================================================================== */

TopicConfig_t            topics         = {};
NetworkConfig_t          network_config = {};
bool                     network_ready      = false;
bool                     wifi_connected     = false;
bool                     mqtt_connected     = false;
esp_mqtt_client_handle_t mqtt_client        = NULL;

static bool    network_provisioned = false;
static bool    provisioning_active = false;
static uint8_t wifi_retry_count    = 0;

static constexpr uint8_t WIFI_MAX_RETRIES = 10;

/* ============================================================================
 * NVS HELPERS
 * ========================================================================== */

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

esp_err_t save_nvs_string(const char *key, const char *value) {
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

esp_err_t clear_network_config_from_nvs(void) {
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

/* ============================================================================
 * NETWORK CONFIG MANAGEMENT
 * ========================================================================== */

static void set_default_network_config(void) {
    uint8_t mac[6] = {};
    memset(&network_config, 0, sizeof(network_config));
    strncpy(network_config.topic_root, DEFAULT_TOPIC_ROOT, sizeof(network_config.topic_root) - 1U);

    if (esp_efuse_mac_get_default(mac) == ESP_OK) {
        snprintf(network_config.device_id, sizeof(network_config.device_id),
                 "device_%02x%02x%02x", mac[3], mac[4], mac[5]);
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
            snprintf(network_config.device_id, sizeof(network_config.device_id),
                     "device_%02x%02x%02x", mac[3], mac[4], mac[5]);
        } else {
            strncpy(network_config.device_id, "device_unknown", sizeof(network_config.device_id) - 1U);
        }
    }
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

    load_nvs_string(handle, NVS_KEY_MQTT_URI,  network_config.mqtt_broker_uri, sizeof(network_config.mqtt_broker_uri));
    load_nvs_string(handle, NVS_KEY_DEVICE_ID, network_config.device_id,       sizeof(network_config.device_id));
    load_nvs_string(handle, NVS_KEY_SETUP_POP, network_config.setup_pop,       sizeof(network_config.setup_pop));
    nvs_close(handle);

    ensure_network_identity_defaults();
    return (network_config.mqtt_broker_uri[0] != '\0') ? ESP_OK : ESP_ERR_NOT_FOUND;
}

static void ensure_setup_pop(void) {
    if (network_config.setup_pop[0] != '\0') {
        return;
    }

    uint8_t mac[6] = {};
    uint32_t random_part = esp_random();
    if (esp_efuse_mac_get_default(mac) == ESP_OK) {
        snprintf(network_config.setup_pop, sizeof(network_config.setup_pop),
                 "%02X%02X%02X%08lX", mac[3], mac[4], mac[5],
                 static_cast<unsigned long>(random_part));
    } else {
        snprintf(network_config.setup_pop, sizeof(network_config.setup_pop),
                 "%08lX%08lX",
                 static_cast<unsigned long>(esp_random()),
                 static_cast<unsigned long>(random_part));
    }

    if (save_nvs_string(NVS_KEY_SETUP_POP, network_config.setup_pop) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to persist setup PoP");
    }
}

/* ============================================================================
 * TOPIC MANAGEMENT
 * ========================================================================== */

static void build_topic(char *buffer, size_t buffer_size, const char *suffix) {
    snprintf(buffer, buffer_size, "%s/%s/%s",
             network_config.topic_root, network_config.device_id, suffix);
}

static void initialize_topics(void) {
    build_topic(topics.cmd_cover,            sizeof(topics.cmd_cover),            "cmd/cover");
    build_topic(topics.cmd_mode,             sizeof(topics.cmd_mode),             "cmd/mode");
    build_topic(topics.cmd_position,         sizeof(topics.cmd_position),         "cmd/position");
    build_topic(topics.cmd_system,           sizeof(topics.cmd_system),           "cmd/system");
    build_topic(topics.state_cover,          sizeof(topics.state_cover),          "state/cover");
    build_topic(topics.state_mode,           sizeof(topics.state_mode),           "state/mode");
    build_topic(topics.state_position,       sizeof(topics.state_position),       "state/position");
    build_topic(topics.state_light_raw,      sizeof(topics.state_light_raw),      "state/light/raw");
    build_topic(topics.state_light_filtered, sizeof(topics.state_light_filtered), "state/light/filtered");
    build_topic(topics.state_light_state,    sizeof(topics.state_light_state),    "state/light/state");
    build_topic(topics.state_motor,          sizeof(topics.state_motor),          "state/motor");
    build_topic(topics.state_slat,           sizeof(topics.state_slat),           "state/slat");
    build_topic(topics.state_health,         sizeof(topics.state_health),         "state/health");
    build_topic(topics.state_network_rssi,   sizeof(topics.state_network_rssi),   "state/network/rssi");
    build_topic(topics.state_network_online, sizeof(topics.state_network_online), "state/network/online");
}

/* ============================================================================
 * MQTT CLIENT
 * ========================================================================== */

static esp_err_t initialize_mqtt_client(void) {
    if (network_config.mqtt_broker_uri[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }
    if (mqtt_client != NULL) {
        return ESP_OK;
    }

    esp_mqtt_client_config_t mqtt_config = {};
    mqtt_config.broker.address.uri  = network_config.mqtt_broker_uri;
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

/* ============================================================================
 * BLE PROVISIONING ENDPOINT
 * ========================================================================== */

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

/* ============================================================================
 * EVENT HANDLERS
 * ========================================================================== */

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

/* ============================================================================
 * INITIALIZATION
 * ========================================================================== */

static void initialize_network_stack(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_PROV_EVENT,                ESP_EVENT_ANY_ID,      &network_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(PROTOCOMM_TRANSPORT_BLE_EVENT,  ESP_EVENT_ANY_ID,      &network_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(PROTOCOMM_SECURITY_SESSION_EVENT, ESP_EVENT_ANY_ID,    &network_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT,                     ESP_EVENT_ANY_ID,      &network_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,                       IP_EVENT_STA_GOT_IP,   &network_event_handler, NULL));

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t wifi_init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init));
}

static void start_wifi_station(void) {
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
}

void initialize_network(void) {
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
    prov_config.scheme               = wifi_prov_scheme_ble;
    prov_config.scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM;
    prov_config.app_event_handler    = WIFI_PROV_EVENT_HANDLER_NONE;
    ESP_ERROR_CHECK(wifi_prov_mgr_init(prov_config));

    bool wifi_provisioned = false;
    ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&wifi_provisioned));
    network_provisioned = wifi_provisioned && (network_config.mqtt_broker_uri[0] != '\0');

    if (!network_provisioned) {
        char service_name[SERVICE_NAME_MAX_LEN + 1] = {};
        /* device_id is clamped to fit: SERVICE_NAME_MAX_LEN - len("SunSense-") = 20 chars */
        snprintf(service_name, sizeof(service_name), "SunSense-%.20s", network_config.device_id);

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
