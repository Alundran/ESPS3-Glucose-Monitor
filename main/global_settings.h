/**
 * Global Settings Storage
 * Manages application settings in NVS
 */

#ifndef GLOBAL_SETTINGS_H
#define GLOBAL_SETTINGS_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

// Default values
#define DEFAULT_LIBRELINK_INTERVAL_MINUTES 2
#define DEFAULT_MOON_LAMP_ENABLED true

/**
 * Global settings structure
 */
typedef struct {
    uint32_t librelink_interval_minutes;  // Update interval in minutes (min 1)
    bool moon_lamp_enabled;               // Enable/disable Moon Lamp IR control
} global_settings_t;

/**
 * Save global settings to NVS
 * @param settings Settings structure to save
 * @return ESP_OK on success
 */
esp_err_t global_settings_save(const global_settings_t *settings);

/**
 * Load global settings from NVS
 * @param settings Output buffer for settings
 * @return ESP_OK on success, uses defaults if not found
 */
esp_err_t global_settings_load(global_settings_t *settings);

/**
 * Check if global settings are stored
 * @return true if settings exist in NVS
 */
bool global_settings_exist(void);

/**
 * Clear stored global settings (resets to defaults)
 * @return ESP_OK on success
 */
esp_err_t global_settings_clear(void);

/**
 * Get current LibreLink update interval in milliseconds
 * Loads from NVS and converts to ms for use with vTaskDelay
 * @return Update interval in milliseconds
 */
uint32_t global_settings_get_interval_ms(void);

/**
 * Check if Moon Lamp is enabled
 * @return true if Moon Lamp IR control is enabled
 */
bool global_settings_is_moon_lamp_enabled(void);

#endif // GLOBAL_SETTINGS_H
