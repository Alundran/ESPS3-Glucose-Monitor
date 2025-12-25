/**
 * Global Settings Storage Implementation
 */

#include "global_settings.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>

static const char *TAG = "GLOBAL_SETTINGS";

#define SETTINGS_NAMESPACE "global_cfg"
#define SETTINGS_KEY "settings"

esp_err_t global_settings_save(const global_settings_t *settings)
{
    if (!settings) {
        return ESP_ERR_INVALID_ARG;
    }

    // Validate settings
    if (settings->librelink_interval_minutes < 1) {
        ESP_LOGE(TAG, "Invalid interval: %lu (must be >= 1)", settings->librelink_interval_minutes);
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(SETTINGS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return err;
    }

    // Save settings as blob
    err = nvs_set_blob(handle, SETTINGS_KEY, settings, sizeof(global_settings_t));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set settings: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    err = nvs_commit(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Settings saved: interval=%lu min, moon_lamp=%s, low=%.1f, high=%.1f", 
                 settings->librelink_interval_minutes,
                 settings->moon_lamp_enabled ? "enabled" : "disabled",
                 settings->glucose_low_threshold,
                 settings->glucose_high_threshold);
    }

    nvs_close(handle);
    return err;
}

esp_err_t global_settings_load(global_settings_t *settings)
{
    if (!settings) {
        return ESP_ERR_INVALID_ARG;
    }

    // Set defaults first
    settings->librelink_interval_minutes = DEFAULT_LIBRELINK_INTERVAL_MINUTES;
    settings->moon_lamp_enabled = DEFAULT_MOON_LAMP_ENABLED;
    settings->glucose_low_threshold = DEFAULT_GLUCOSE_LOW_THRESHOLD;
    settings->glucose_high_threshold = DEFAULT_GLUCOSE_HIGH_THRESHOLD;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(SETTINGS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGI(TAG, "No settings found, using defaults");
            return ESP_OK;  // Return OK with defaults
        }
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return err;
    }

    size_t required_size = sizeof(global_settings_t);
    err = nvs_get_blob(handle, SETTINGS_KEY, settings, &required_size);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGI(TAG, "No settings found, using defaults");
            nvs_close(handle);
            return ESP_OK;  // Return OK with defaults
        }
        ESP_LOGE(TAG, "Failed to get settings: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    // Validate loaded settings
    if (settings->librelink_interval_minutes < 1) {
        ESP_LOGW(TAG, "Invalid interval loaded, resetting to default");
        settings->librelink_interval_minutes = DEFAULT_LIBRELINK_INTERVAL_MINUTES;
    }

    ESP_LOGI(TAG, "Settings loaded: interval=%lu min, moon_lamp=%s, low=%.1f, high=%.1f",
             settings->librelink_interval_minutes,
             settings->moon_lamp_enabled ? "enabled" : "disabled",
             settings->glucose_low_threshold,
             settings->glucose_high_threshold);

    nvs_close(handle);
    return ESP_OK;
}

bool global_settings_exist(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(SETTINGS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return false;
    }

    size_t required_size = 0;
    err = nvs_get_blob(handle, SETTINGS_KEY, NULL, &required_size);
    nvs_close(handle);

    return (err == ESP_OK && required_size == sizeof(global_settings_t));
}

esp_err_t global_settings_clear(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(SETTINGS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_erase_key(handle, SETTINGS_KEY);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "Failed to erase settings: %s", esp_err_to_name(err));
    } else {
        err = nvs_commit(handle);
        ESP_LOGI(TAG, "Settings cleared");
    }

    nvs_close(handle);
    return err;
}

uint32_t global_settings_get_interval_ms(void)
{
    global_settings_t settings;
    if (global_settings_load(&settings) != ESP_OK) {
        return DEFAULT_LIBRELINK_INTERVAL_MINUTES * 60 * 1000;
    }
    return settings.librelink_interval_minutes * 60 * 1000;
}

bool global_settings_is_moon_lamp_enabled(void)
{
    global_settings_t settings;
    if (global_settings_load(&settings) != ESP_OK) {
        return DEFAULT_MOON_LAMP_ENABLED;
    }
    return settings.moon_lamp_enabled;
}
