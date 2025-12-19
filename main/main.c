/**
 * ESP32-S3-BOX-3 Glucose Monitor
 * 
 * Built by Spalding for the Supreme (Stephen Higgins)
 * Features: Display, WiFi provisioning, LibreLinkUp API, OTA Updates
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "config.h"
#include "display.h"
#include "wifi_manager.h"
#include "librelinkup.h"
#include "libre_credentials.h"
#include "global_settings.h"
#include "ir_transmitter.h"
#include "ota_update.h"
#include "bsp/esp-bsp.h"
#include "iot_button.h"

static const char *TAG = "GLUCOSE_MONITOR";

// WiFi status tracking
static bool wifi_ready = false;
static bool setup_in_progress = false;
static bool settings_shown = false;

// LibreLink/Glucose tracking
static bool libre_logged_in = false;
static char libre_patient_id[64] = {0};
static libre_glucose_data_t current_glucose = {0};

// Forward declarations
static void on_about_next_button(void);
static void on_setup_next_button(void);
static void on_retry_button(void);
static void on_restart_setup_button(void);
static void on_reset_button(void);
static void red_button_handler(void *arg, void *data);
static void on_ota_proceed(void);
static void on_ota_cancel(void);
static void ota_progress_callback(int progress_percent, const char *message);
static void check_for_ota_update(void);

// Callbacks for WiFi events
static void on_wifi_connected(void) {
    wifi_ready = true;
    const char *ssid = wifi_manager_get_ssid();
    const char *ip = wifi_manager_get_ip();
    ESP_LOGI(TAG, "WiFi Connected - SSID: %s, IP: %s", ssid, ip);
    
    if (setup_in_progress) {
        // User is on setup screen - show Next button
        display_setup_wifi_connected();
    } else {
        // Check if LibreLink credentials exist OR demo mode is enabled
        if (DEMO_MODE_ENABLED) {
            // Demo mode - show demo glucose reading
            display_show_glucose(DEMO_GLUCOSE_MMOL, DEMO_TREND, DEMO_GLUCOSE_LOW, DEMO_GLUCOSE_HIGH);
        } else if (libre_credentials_exist()) {
            // Real credentials - show loading message while fetching
            display_show_wifi_status("Loading glucose data...");
        } else {
            // No LibreLink credentials - show QR code
            display_show_librelink_qr(ip);
        }
    }
}

static void on_wifi_disconnected(void) {
    wifi_ready = false;
    // Only show disconnected if we're not in initial connection phase
    if (setup_in_progress) {
        display_show_wifi_status("WiFi Disconnected");
    }
}

static void on_wifi_failed(void) {
    ESP_LOGE(TAG, "WiFi connection failed after max retries");
    wifi_ready = false;
    display_show_connection_failed(on_retry_button, on_restart_setup_button);
}

// Button callbacks for connection failed screen
static void on_retry_button(void) {
    ESP_LOGI(TAG, "Retry button pressed - attempting to reconnect");
    display_show_wifi_status("Retrying connection...");
    esp_restart();
}

static void on_restart_setup_button(void) {
    ESP_LOGI(TAG, "Restart Setup button pressed - clearing credentials");
    wifi_manager_clear_credentials();
    display_show_wifi_status("Restarting setup...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

// Button callbacks for setup flow
static void on_about_next_button(void) {
    ESP_LOGI(TAG, "About Next button pressed - switching to AP mode for setup");
    setup_in_progress = true;
    
    // If not already in AP mode, switch to it
    if (wifi_manager_is_connected() || wifi_manager_is_provisioned()) {
        // Currently in STA mode, need to switch to AP
        wifi_manager_start_ap_mode();
    }
    
    // Show setup screen with instructions
    display_show_setup(on_setup_next_button);
}

static void on_setup_next_button(void) {
    ESP_LOGI(TAG, "Setup Next button pressed - checking WiFi connection");
    
    if (wifi_manager_is_connected()) {
        ESP_LOGI(TAG, "WiFi connected! Continuing to main app...");
        display_show_wifi_status("Connected!\nInitializing...");
        setup_in_progress = false;
        
        // Show appropriate screen based on credentials/demo mode
        vTaskDelay(pdMS_TO_TICKS(2000));
        if (DEMO_MODE_ENABLED) {
            // Demo mode - show demo glucose
            display_show_glucose(DEMO_GLUCOSE_MMOL, DEMO_TREND, DEMO_GLUCOSE_LOW, DEMO_GLUCOSE_HIGH);
        } else if (libre_credentials_exist()) {
            // Real credentials - show loading message
            display_show_wifi_status("Loading glucose data...");
        } else {
            // No credentials - show setup message
            const char *ip = wifi_manager_get_ip();
            display_show_librelink_qr(ip);
        }
    } else {
        ESP_LOGI(TAG, "WiFi not connected yet, please wait...");
        display_show_wifi_status("Not connected yet.\nPlease configure WiFi\nvia the web portal\nand try again.");
        
        // Wait and show setup screen again
        vTaskDelay(pdMS_TO_TICKS(3000));
        display_show_setup(on_setup_next_button);
    }
}

// Red button handler - toggles settings
static void red_button_handler(void *arg, void *data) {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "RED BUTTON PRESSED - TOGGLE SETTINGS");
    ESP_LOGI(TAG, "========================================");
    
    if (settings_shown) {
        // Go back to glucose display or credentials message
        settings_shown = false;
        if (DEMO_MODE_ENABLED) {
            // Demo mode - show demo glucose
            display_show_glucose(current_glucose.value_mmol > 0 ? current_glucose.value_mmol : 6.7, 
                               librelinkup_get_trend_string(current_glucose.trend), 
                               current_glucose.is_low, current_glucose.is_high);
        } else if (libre_credentials_exist()) {
            // Real credentials - show current glucose if we have it, otherwise loading
            if (current_glucose.value_mmol > 0) {
                display_show_glucose(current_glucose.value_mmol, 
                                   librelinkup_get_trend_string(current_glucose.trend), 
                                   current_glucose.is_low, current_glucose.is_high);
            } else {
                display_show_wifi_status("Loading glucose data...");
            }
        } else {
            const char *ip = wifi_manager_get_ip();
            display_show_librelink_qr(ip);
        }
    } else {
        // Show settings
        settings_shown = true;
        display_show_settings(on_reset_button);
    }
}

// Settings screen callback
static void on_reset_button(void) {
    ESP_LOGI(TAG, "Reset button pressed - clearing credentials and restarting");
    wifi_manager_clear_credentials();
    libre_credentials_clear();
    display_show_wifi_status("Resetting device...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

// OTA update callbacks
static char new_ota_version[32] = {0};

static void on_ota_proceed(void) {
    ESP_LOGI(TAG, "User confirmed OTA update");
    // Show progress screen immediately
    display_show_ota_progress(0, "Preparing update...");
    // Give LVGL time to render the screen before blocking OTA operation
    vTaskDelay(pdMS_TO_TICKS(100));
    // Start the update (this will call ota_progress_callback with updates)
    ota_perform_update(ota_progress_callback);
}

static void on_ota_cancel(void) {
    ESP_LOGI(TAG, "User cancelled OTA update");
    // Return to glucose display or appropriate screen
    if (DEMO_MODE_ENABLED) {
        display_show_glucose(current_glucose.value_mmol > 0 ? current_glucose.value_mmol : 6.7, 
                           librelinkup_get_trend_string(current_glucose.trend), 
                           current_glucose.is_low, current_glucose.is_high);
    } else if (libre_credentials_exist()) {
        if (current_glucose.value_mmol > 0) {
            display_show_glucose(current_glucose.value_mmol, 
                               librelinkup_get_trend_string(current_glucose.trend), 
                               current_glucose.is_low, current_glucose.is_high);
        } else {
            display_show_wifi_status("Loading glucose data...");
        }
    } else {
        const char *ip = wifi_manager_get_ip();
        display_show_librelink_qr(ip);
    }
}

static void ota_progress_callback(int progress_percent, const char *message) {
    display_show_ota_progress(progress_percent, message);
}

static void ota_check_task(void *pvParameters) {
    vTaskDelay(pdMS_TO_TICKS(5000));  // Wait 5s after boot
    check_for_ota_update();
    vTaskDelete(NULL);
}

static void check_for_ota_update(void) {
    if (!wifi_manager_is_connected()) {
        ESP_LOGW(TAG, "Skipping OTA check - WiFi not connected");
        return;
    }
    
    ESP_LOGI(TAG, "Checking for OTA updates on boot...");
    esp_err_t ret = ota_check_for_update(new_ota_version, sizeof(new_ota_version));
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "OTA update available: %s -> %s", ota_get_current_version(), new_ota_version);
        // Show warning dialog to user
        display_show_ota_warning(on_ota_proceed, on_ota_cancel);
    } else if (ret == ESP_ERR_NOT_FOUND) {
        ESP_LOGI(TAG, "Already running latest firmware version");
    } else {
        ESP_LOGW(TAG, "Failed to check for OTA update: %s", esp_err_to_name(ret));
    }
}

// Task to periodically fetch glucose data from LibreLinkUp
static void glucose_fetch_task(void *pvParameters) {
#if !DEMO_MODE_ENABLED
    char email[128] = {0};
    char password[128] = {0};
    bool use_eu_server = false;
#endif
    
    while (1) {
        // Wait configured interval between updates (load from settings each time)
        uint32_t interval_ms = global_settings_get_interval_ms();
        ESP_LOGI(TAG, "Next glucose update in %lu minutes", interval_ms / 60000);
        vTaskDelay(pdMS_TO_TICKS(interval_ms));
        
        // Only fetch if WiFi is connected and (credentials exist OR demo mode)
        if (!wifi_ready || (!libre_credentials_exist() && !DEMO_MODE_ENABLED)) {
            continue;
        }
        
        // Load credentials if not logged in
        if (!libre_logged_in) {
#if DEMO_MODE_ENABLED
            // In demo mode, just set the flags without any API calls
            ESP_LOGI(TAG, "[DEMO MODE] Using demo data - no API initialization");
            libre_logged_in = true;
            strcpy(libre_patient_id, "demo-patient");
#else
            if (libre_credentials_load(email, password, libre_patient_id, &use_eu_server) == ESP_OK) {
                ESP_LOGI(TAG, "Loading LibreLink credentials...");
                librelinkup_init(use_eu_server);
                
                if (librelinkup_login(email, password) == ESP_OK) {
                    ESP_LOGI(TAG, "LibreLink login successful");
                    libre_logged_in = true;
                    
                    // If no patient ID stored, get it now
                    if (libre_patient_id[0] == '\0') {
                        if (librelinkup_get_patient_id(libre_patient_id, sizeof(libre_patient_id)) == ESP_OK) {
                            ESP_LOGI(TAG, "Got patient ID: %s", libre_patient_id);
                            // Save it for next time
                            libre_credentials_save(email, password, libre_patient_id, use_eu_server);
                        }
                    }
                } else {
                    ESP_LOGE(TAG, "LibreLink login failed");
                    continue;
                }
            }
#endif
        }
        
        // Fetch glucose data
        if (libre_logged_in && libre_patient_id[0] != '\0') {
            ESP_LOGI(TAG, "Fetching glucose data...");
            if (librelinkup_get_glucose(libre_patient_id, &current_glucose) == ESP_OK) {
                ESP_LOGI(TAG, "Glucose: %d mg/dL, Trend: %s", 
                        current_glucose.value_mgdl, 
                        librelinkup_get_trend_string(current_glucose.trend));
                
                // Update display if not in settings
                if (!settings_shown && !setup_in_progress) {
                    display_show_glucose(current_glucose.value_mmol, 
                                       librelinkup_get_trend_string(current_glucose.trend),
                                       current_glucose.is_low, current_glucose.is_high);
                }
            } else {
                ESP_LOGE(TAG, "Failed to fetch glucose data");
                libre_logged_in = false;  // Force re-login next time
            }
        }
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  %s v%s", DEVICE_NAME, DEVICE_VERSION);
    ESP_LOGI(TAG, "  Built by %s for %s", DEVICE_MANUFACTURER, DEVICE_OWNER);
    ESP_LOGI(TAG, "========================================");
    
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Initialize display first
    ESP_LOGI(TAG, "Initializing display...");
    ESP_ERROR_CHECK(display_init());
    
    // Create LVGL task with larger stack
    xTaskCreate(display_task, "display_task", 8192, NULL, 5, NULL);
    
    // Give LVGL task time to start
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Show splash screen
    display_show_splash();
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // Initialize buttons (red button on LCD panel) - MUST be after display init
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Initializing buttons...");
    ESP_LOGI(TAG, "BSP_BUTTON_NUM=%d, BSP_BUTTON_MAIN=%d", BSP_BUTTON_NUM, BSP_BUTTON_MAIN);
    button_handle_t btns[BSP_BUTTON_NUM];
    int btn_cnt = 0;
    esp_err_t btn_err = bsp_iot_button_create(btns, &btn_cnt, BSP_BUTTON_NUM);
    ESP_LOGI(TAG, "bsp_iot_button_create returned: %s", esp_err_to_name(btn_err));
    if (btn_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize buttons: %s", esp_err_to_name(btn_err));
    } else {
        ESP_LOGI(TAG, "Initialized %d buttons successfully", btn_cnt);
        
        // Log all button handles
        for (int i = 0; i < btn_cnt; i++) {
            ESP_LOGI(TAG, "Button[%d] handle: %p", i, btns[i]);
        }
        
        // Register red button (MAIN button) callback - only for single click to avoid double triggers
        if (btn_cnt > BSP_BUTTON_MAIN && btns[BSP_BUTTON_MAIN] != NULL) {
            ESP_LOGI(TAG, "Attempting to register callback for BSP_BUTTON_MAIN (index %d, handle %p)", 
                     BSP_BUTTON_MAIN, btns[BSP_BUTTON_MAIN]);
            
            esp_err_t cb_err = iot_button_register_cb(btns[BSP_BUTTON_MAIN], BUTTON_SINGLE_CLICK, red_button_handler, NULL);
            ESP_LOGI(TAG, "SINGLE_CLICK registration: %s", esp_err_to_name(cb_err));
        } else {
            ESP_LOGE(TAG, "Red button not available! btn_cnt=%d, BSP_BUTTON_MAIN=%d, handle=%p", 
                     btn_cnt, BSP_BUTTON_MAIN, btn_cnt > BSP_BUTTON_MAIN ? btns[BSP_BUTTON_MAIN] : NULL);
        }
    }
    ESP_LOGI(TAG, "========================================");
    
    // Initialize WiFi manager
    ESP_LOGI(TAG, "Initializing WiFi...");
    wifi_manager_register_connected_cb(on_wifi_connected);
    wifi_manager_register_disconnected_cb(on_wifi_disconnected);
    wifi_manager_register_failed_cb(on_wifi_failed);
    ESP_ERROR_CHECK(wifi_manager_init());
    
    // Initialize OTA update system
    ESP_LOGI(TAG, "Initializing OTA update system...");
    ota_update_init();
    
    // Initialize IR transmitter if Moon Lamp is enabled
    if (global_settings_is_moon_lamp_enabled()) {
        ESP_LOGI(TAG, "Initializing IR transmitter for Moon Lamp...");
        esp_err_t ir_ret = ir_transmitter_init();
        if (ir_ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize IR transmitter: %s", esp_err_to_name(ir_ret));
        } else {
            ESP_LOGI(TAG, "IR transmitter initialized successfully");
        }
    } else {
        ESP_LOGI(TAG, "Moon Lamp disabled in settings - skipping IR transmitter init");
    }
    
    // Start glucose fetch task
    xTaskCreate(glucose_fetch_task, "glucose_fetch", 8192, NULL, 4, NULL);
    
    // If credentials exist, wait to see if connection succeeds
    if (wifi_manager_is_provisioned()) {
        ESP_LOGI(TAG, "WiFi credentials found, waiting for connection...");
        // Keep splash screen visible while connecting
        
        // Wait up to 20 seconds for connection (IP address takes time)
        for (int i = 0; i < 40; i++) {
            vTaskDelay(pdMS_TO_TICKS(500));
            if (wifi_manager_is_connected()) {
                ESP_LOGI(TAG, "Connected to WiFi successfully");
                // Check for OTA updates on boot (non-blocking)
                xTaskCreate(ota_check_task, "ota_check", 4096, NULL, 3, NULL);
                
                // Callback will handle display (show glucose directly)
                return;
            }
            // Log every 2 seconds to show we're waiting
            if (i % 4 == 0) {
                ESP_LOGI(TAG, "Waiting for WiFi connection... (%d/20s)", i/2);
            }
        }
        
        // Connection failed - show connection failed screen
        ESP_LOGE(TAG, "WiFi connection timeout - no IP address received");
        display_show_connection_failed(on_retry_button, on_restart_setup_button);
        // Don't return - let the task continue running
    } else {
        // No credentials - AP mode already started, show About screen
        ESP_LOGI(TAG, "No WiFi credentials, AP mode active, showing About screen");
        display_show_about(on_about_next_button);
    }
    
    ESP_LOGI(TAG, "Initialization complete");
    ESP_LOGI(TAG, "========================================\n");
}
