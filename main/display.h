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
 * @param measurement_color Color indicator (1=green/normal, 2=amber/warning, 3=red/hypo)
 */
void display_show_glucose(float glucose_mmol, const char *trend, bool is_low, bool is_high, const char *timestamp, int measurement_color);

/**
 * Show "No recent data" message with orange background
 */
void display_show_no_recent_data(void);

/**
 * Show date/time/moon phase screen (activated by slide-down gesture)
 */
void display_show_datetime_moon(void);

/**
 * Show random quote screen (activated by slide-up gesture)
 */
void display_show_random_quote(void);

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
 * @param about_cb Callback for About button
 */
void display_show_settings(display_button_callback_t reset_cb, display_button_callback_t about_cb);

/**
 * Show about screen with message
 * @param back_cb Callback for Back button
 */
void display_show_about_message(display_button_callback_t back_cb);

/**
 * Show LibreLink setup screen with QR code
 * @param ip IP address of the device
 */
void display_show_librelink_qr(const char *ip);

/**
 * Show OTA update progress screen
 * @param progress_percent Progress percentage (0-100)
 * @param message Status message
 */
void display_show_ota_progress(int progress_percent, const char *message);

/**
 * Show OTA update warning screen
 * @param proceed_cb Callback when user confirms to proceed
 * @param cancel_cb Callback when user cancels
 */
void display_show_ota_warning(display_button_callback_t proceed_cb, display_button_callback_t cancel_cb);

/**
 * Transition OTA warning screen to updating state
 * Removes buttons and changes text to "Updating..."
 */
void display_ota_warning_start_update(void);

#endif // DISPLAY_H
