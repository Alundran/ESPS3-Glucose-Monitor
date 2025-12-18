/**
 * WiFi Manager for ESP32-S3-BOX-3
 * Handles WiFi connection with fallback AP mode for provisioning
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "esp_err.h"
#include <stdbool.h>

// Callback function types
typedef void (*wifi_connected_cb_t)(void);
typedef void (*wifi_disconnected_cb_t)(void);
typedef void (*wifi_failed_cb_t)(void);

/**
 * Initialize WiFi manager
 * Attempts to connect to saved WiFi or starts AP mode
 */
esp_err_t wifi_manager_init(void);

/**
 * Check if WiFi is connected
 */
bool wifi_manager_is_connected(void);

/**
 * Get current SSID
 */
const char* wifi_manager_get_ssid(void);

/**
 * Get current IP address
 */
const char* wifi_manager_get_ip(void);

/**
 * Register callback for WiFi connected event
 */
void wifi_manager_register_connected_cb(wifi_connected_cb_t cb);

/**
 * Register callback for WiFi disconnected event
 */
void wifi_manager_register_disconnected_cb(wifi_disconnected_cb_t cb);

/**
 * Register callback for WiFi connection failed event (after max retries)
 */
void wifi_manager_register_failed_cb(wifi_failed_cb_t cb);

/**
 * Clear stored WiFi credentials
 */
esp_err_t wifi_manager_clear_credentials(void);

/**
 * Check if WiFi credentials are stored in NVS
 */
bool wifi_manager_is_provisioned(void);

/**
 * Start AP mode for WiFi provisioning
 */
esp_err_t wifi_manager_start_ap_mode(void);

#endif // WIFI_MANAGER_H
