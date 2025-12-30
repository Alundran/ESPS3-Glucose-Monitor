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
#define DEFAULT_GLUCOSE_LOW_THRESHOLD 3.9
#define DEFAULT_GLUCOSE_HIGH_THRESHOLD 13.3
#define DEFAULT_ALARM_ENABLED true
#define DEFAULT_ALARM_SNOOZE_MINUTES 5
#define DEFAULT_ALARM_LOW_ENABLED true
#define DEFAULT_ALARM_HIGH_ENABLED false

// Settings version - increment when structure changes
#define GLOBAL_SETTINGS_VERSION 5

/**
 * Global settings structure
 */
typedef struct {
    uint32_t version;                     // Settings version for migration
    uint32_t librelink_interval_minutes;  // Update interval in minutes (min 1)
    bool moon_lamp_enabled;               // Enable/disable Moon Lamp IR control
    float glucose_low_threshold;          // Low glucose threshold in mmol/L
    float glucose_high_threshold;         // High glucose threshold in mmol/L
    bool alarm_enabled;                   // Enable/disable threshold alarm
    uint32_t alarm_snooze_minutes;        // Alarm snooze duration in minutes (1-60)
    bool alarm_low_enabled;               // Enable/disable LOW glucose alarm
    bool alarm_high_enabled;              // Enable/disable HIGH glucose alarm
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
