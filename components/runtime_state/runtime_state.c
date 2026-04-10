/**
 * @file runtime_state.c
 * @brief Persistent runtime state storage for SunSense
 */

#include "runtime_state.h"

#include "nvs.h"
#include "nvs_flash.h"

#include <string.h>

#define RUNTIME_NAMESPACE "runtime"
#define KEY_VERSION "version"
#define KEY_POS_VALID "pos_valid"
#define KEY_POSITION "position"
#define KEY_MODE "mode"
#define KEY_LIGHT "light"
#define KEY_SLAT_VALID "slat_valid"
#define KEY_SLAT_ANGLE "slat_angle"

static bool ensure_nvs_ready(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

    return (err == ESP_OK || err == ESP_ERR_INVALID_STATE);
}

bool runtime_state_init(RuntimeStateController_t *controller) {
    if (controller == NULL) {
        return false;
    }

    memset(controller, 0, sizeof(*controller));
    controller->initialized = ensure_nvs_ready();
    return controller->initialized;
}

bool runtime_state_load(RuntimeStateController_t *controller, RuntimeStateSnapshot_t *snapshot) {
    if ((controller == NULL) || !controller->initialized || (snapshot == NULL)) {
        return false;
    }

    memset(snapshot, 0, sizeof(*snapshot));

    nvs_handle_t handle = 0;
    if (nvs_open(RUNTIME_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }

    uint8_t version = 0;
    esp_err_t err = nvs_get_u8(handle, KEY_VERSION, &version);
    if (err != ESP_OK || version != RUNTIME_STATE_SCHEMA_VERSION) {
        nvs_close(handle);
        return false;
    }

    uint8_t pos_valid = 0;
    uint8_t position = 0;
    uint8_t mode = MODE_AUTO;
    uint8_t light = LIGHT_DARK;
    uint8_t slat_valid = 0;
    uint8_t slat_angle = 0;

    bool ok =
        (nvs_get_u8(handle, KEY_POS_VALID, &pos_valid) == ESP_OK) &&
        (nvs_get_u8(handle, KEY_POSITION, &position) == ESP_OK) &&
        (nvs_get_u8(handle, KEY_MODE, &mode) == ESP_OK) &&
        (nvs_get_u8(handle, KEY_LIGHT, &light) == ESP_OK) &&
        (nvs_get_u8(handle, KEY_SLAT_VALID, &slat_valid) == ESP_OK) &&
        (nvs_get_u8(handle, KEY_SLAT_ANGLE, &slat_angle) == ESP_OK);

    nvs_close(handle);

    if (!ok) {
        return false;
    }

    snapshot->state_version = version;
    snapshot->position_valid = (pos_valid != 0U);
    snapshot->position_percent = position;
    snapshot->mode = (mode == MODE_MANUAL) ? MODE_MANUAL : MODE_AUTO;
    snapshot->light_level = (light == LIGHT_BRIGHT) ? LIGHT_BRIGHT : LIGHT_DARK;
    snapshot->slat_angle_valid = (slat_valid != 0U);
    snapshot->slat_angle_deg = slat_angle;
    return true;
}

bool runtime_state_save(RuntimeStateController_t *controller, const RuntimeStateSnapshot_t *snapshot) {
    if ((controller == NULL) || !controller->initialized || (snapshot == NULL)) {
        return false;
    }

    nvs_handle_t handle = 0;
    if (nvs_open(RUNTIME_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        return false;
    }

    esp_err_t err = nvs_set_u8(handle, KEY_VERSION, RUNTIME_STATE_SCHEMA_VERSION);
    if (err == ESP_OK) {
        err = nvs_set_u8(handle, KEY_POS_VALID, snapshot->position_valid ? 1U : 0U);
    }
    if (err == ESP_OK) {
        err = nvs_set_u8(handle, KEY_POSITION, snapshot->position_percent);
    }
    if (err == ESP_OK) {
        err = nvs_set_u8(handle, KEY_MODE, (uint8_t)snapshot->mode);
    }
    if (err == ESP_OK) {
        err = nvs_set_u8(handle, KEY_LIGHT, (uint8_t)snapshot->light_level);
    }
    if (err == ESP_OK) {
        err = nvs_set_u8(handle, KEY_SLAT_VALID, snapshot->slat_angle_valid ? 1U : 0U);
    }
    if (err == ESP_OK) {
        err = nvs_set_u8(handle, KEY_SLAT_ANGLE, snapshot->slat_angle_deg);
    }

    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);
    return (err == ESP_OK);
}
