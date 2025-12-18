/**
 * Display Manager for ESP32-S3-BOX-3
 * Handles display initialization and screen management
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#include "esp_err.h"
#include <stdbool.h>

/**
 * Initialize the display hardware and LVGL
 */
esp_err_t display_init(void);

/**
 * LVGL task handler - must be called periodically
 * Run this in a FreeRTOS task
 */
void display_task(void *pvParameters);

/**
 * Show splash screen with title
 */
void display_show_splash(void);

/**
 * Show WiFi status message
 */
void display_show_wifi_status(const char *message);

/**
 * Show glucose level with trend indicator
 * @param glucose_level Glucose level in mg/dL
 * @param trend Trend arrow (e.g., "↑", "→", "↓")
 * @param is_low True if glucose is low (<70)
 * @param is_high True if glucose is high (>180)
 */
void display_show_glucose(float glucose_mmol, const char *trend, bool is_low, bool is_high);

/**
 * Button callback function type
 */
typedef void (*display_button_callback_t)(void);

/**
 * Show About screen with Next button
 * @param callback Function to call when Next button is pressed
 */
void display_show_about(display_button_callback_t callback);

/**
 * Show WiFi setup instructions with Next button
 * @param callback Function to call when Next button is pressed
 */
void display_show_setup(display_button_callback_t callback);

/**
 * Update setup screen when WiFi connects - hide spinner, show Next button
 */
void display_setup_wifi_connected(void);

/**
 * Show connection failed screen with Retry and Setup buttons
 * @param retry_cb Callback for Retry button
 * @param setup_cb Callback for Setup button
 */
void display_show_connection_failed(display_button_callback_t retry_cb, display_button_callback_t setup_cb);

/**
 * Show settings screen
 * @param reset_cb Callback for Reset button
 */
void display_show_settings(display_button_callback_t reset_cb);

#endif // DISPLAY_H
