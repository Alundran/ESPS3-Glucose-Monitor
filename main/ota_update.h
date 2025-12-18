/**
 * OTA Update Module
 * 
 * Checks GitHub for firmware updates and performs OTA installation
 * Supports ESP32-S3-BOX-3 with dual OTA partitions
 */

#ifndef OTA_UPDATE_H
#define OTA_UPDATE_H

#include "esp_err.h"
#include <stdbool.h>

// GitHub repository information
#define GITHUB_REPO_OWNER "Alundran"
#define GITHUB_REPO_NAME "ESPS3-Glucose-Monitor"
#define GITHUB_API_URL "https://api.github.com/repos/" GITHUB_REPO_OWNER "/" GITHUB_REPO_NAME "/releases/latest"

/**
 * OTA update progress callback
 * @param progress_percent Current download/install progress (0-100)
 * @param message Status message
 */
typedef void (*ota_progress_callback_t)(int progress_percent, const char *message);

/**
 * Initialize OTA update system
 * Must be called after WiFi is connected
 * 
 * @return ESP_OK on success
 */
esp_err_t ota_update_init(void);

/**
 * Check if a firmware update is available on GitHub
 * Compares latest release version with current firmware version
 * 
 * @param[out] new_version Buffer to store new version string (e.g., "1.0.1")
 * @param new_version_size Size of new_version buffer
 * @return ESP_OK if update available, ESP_ERR_NOT_FOUND if already latest, other errors on failure
 */
esp_err_t ota_check_for_update(char *new_version, size_t new_version_size);

/**
 * Download and install firmware update from GitHub
 * Shows progress via callback
 * IMPORTANT: Device must have stable power - DO NOT DISCONNECT during update!
 * 
 * @param progress_cb Callback function for progress updates (can be NULL)
 * @return ESP_OK on success (device will reboot), error code on failure
 */
esp_err_t ota_perform_update(ota_progress_callback_t progress_cb);

/**
 * Check if device has stable power and WiFi before updating
 * 
 * @return true if safe to update (WiFi connected and power good)
 */
bool ota_is_safe_to_update(void);

/**
 * Get current firmware version string
 * 
 * @return Version string (e.g., "1.0.0")
 */
const char* ota_get_current_version(void);

#endif // OTA_UPDATE_H
