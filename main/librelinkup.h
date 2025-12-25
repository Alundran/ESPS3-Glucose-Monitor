/**
 * LibreLinkUp API Client for ESP32
 * 
 * Based on: https://gist.github.com/khskekec/6c13ba01b10d3018d816706a32ae8ab2
 */

#ifndef LIBRELINKUP_H
#define LIBRELINKUP_H

#include "esp_err.h"
#include <stdbool.h>

// Custom error codes (using custom base 0x6000 for application-specific errors)
#define ESP_ERR_LIBRE_RATE_LIMITED    0x6001  // Rate limited (429)
#define ESP_ERR_LIBRE_AUTH_FAILED     0x6002  // Authentication failed (401)

// API configuration
#define LIBRELINKUP_API_URL_GLOBAL "https://api.libreview.io"
#define LIBRELINKUP_API_URL_EU     "https://api-eu.libreview.io"

// Trend arrows
typedef enum {
    LIBRE_TREND_NONE = 0,
    LIBRE_TREND_RISING_QUICKLY = 1,
    LIBRE_TREND_RISING = 2,
    LIBRE_TREND_STABLE = 3,
    LIBRE_TREND_FALLING = 4,
    LIBRE_TREND_FALLING_QUICKLY = 5
} libre_trend_t;

// Glucose measurement data
typedef struct {
    int value_mgdl;          // Glucose value in mg/dL
    float value_mmol;        // Glucose value in mmol/L (mg/dL / 18)
    libre_trend_t trend;     // Trend arrow
    bool is_high;            // High glucose flag
    bool is_low;             // Low glucose flag
    char timestamp[32];      // Timestamp string
    int measurement_color;   // Measurement color (1=normal, 2=high, 0=low)
    int type;                // Measurement type
} libre_glucose_data_t;

/**
 * Initialize LibreLinkUp client
 * @param use_eu_server Set true to use EU server, false for global
 */
esp_err_t librelinkup_init(bool use_eu_server);

/**
 * Login to LibreLinkUp and obtain authentication token
 * @param email User email address
 * @param password User password
 * @return ESP_OK on success
 */
esp_err_t librelinkup_login(const char *email, const char *password);

/**
 * Get the first patient ID from connections
 * This is typically used when following one person's glucose data
 * @param patient_id Output buffer for patient ID (min 64 bytes)
 * @return ESP_OK on success
 */
esp_err_t librelinkup_get_patient_id(char *patient_id, size_t patient_id_len);

/**
 * Get connections list as JSON for web interface
 * Returns: {"success":true,"patients":[{"id":"abc","name":"John Doe"},...]}
 * @param json_buffer Output buffer for JSON string
 * @param buffer_size Size of output buffer
 * @return ESP_OK on success
 */
esp_err_t librelinkup_get_connections_json(char *json_buffer, size_t buffer_size);

/**
 * Get latest glucose reading for a patient
 * @param patient_id Patient ID from librelinkup_get_patient_id()
 * @param glucose_data Output structure for glucose data
 * @return ESP_OK on success
 */
esp_err_t librelinkup_get_glucose(const char *patient_id, libre_glucose_data_t *glucose_data);

/**
 * Check if currently logged in
 * @return true if logged in with valid token
 */
bool librelinkup_is_logged_in(void);

/**
 * Logout and clear authentication token
 */
void librelinkup_logout(void);

/**
 * Get trend arrow as string for display
 * @param trend Trend value
 * @return Arrow string (↑, →, ↓, etc.)
 */
const char* librelinkup_get_trend_string(libre_trend_t trend);

/**
 * Convert mg/dL to mmol/L
 * @param mgdl Value in mg/dL
 * @return Value in mmol/L
 */
float librelinkup_mgdl_to_mmol(int mgdl);

#endif // LIBRELINKUP_H
