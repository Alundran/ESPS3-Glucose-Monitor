/**
 * LibreLink Credentials Storage
 * Manages LibreLinkUp login credentials in NVS
 */

#ifndef LIBRE_CREDENTIALS_H
#define LIBRE_CREDENTIALS_H

#include "esp_err.h"
#include <stdbool.h>

/**
 * Save LibreLink credentials to NVS
 * @param email User email
 * @param password User password
 * @param patient_id Patient ID (optional, can be NULL)
 * @param use_eu_server Whether to use EU server
 * @return ESP_OK on success
 */
esp_err_t libre_credentials_save(const char *email, const char *password, 
                                  const char *patient_id, bool use_eu_server);

/**
 * Load LibreLink credentials from NVS
 * @param email Output buffer for email (min 128 bytes)
 * @param password Output buffer for password (min 128 bytes)
 * @param patient_id Output buffer for patient ID (min 64 bytes, can be NULL)
 * @param use_eu_server Output for server selection
 * @return ESP_OK on success
 */
esp_err_t libre_credentials_load(char *email, char *password, 
                                  char *patient_id, bool *use_eu_server);

/**
 * Check if LibreLink credentials are stored
 * @return true if credentials exist
 */
bool libre_credentials_exist(void);

/**
 * Clear stored LibreLink credentials
 * @return ESP_OK on success
 */
esp_err_t libre_credentials_clear(void);

#endif // LIBRE_CREDENTIALS_H
