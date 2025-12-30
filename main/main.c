/**
 * ESP32-S3-BOX-3 Glucose Monitor
 * 
 * Built by Spalding for the Supreme (Stephen Higgins)
 * Features: Display, WiFi provisioning, LibreLinkUp API, OTA Updates
 */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_timer.h"
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
#include "esp_codec_dev.h"

static const char *TAG = "GLUCOSE_MONITOR";

// WiFi status tracking
static bool wifi_ready = false;
static bool setup_in_progress = false;
static bool settings_shown = false;
static volatile bool ota_check_complete = false;
static volatile bool ota_in_progress = false;  // Prevents glucose updates during OTA

// LibreLink/Glucose tracking
static bool libre_logged_in = false;
static char libre_patient_id[64] = {0};
static libre_glucose_data_t current_glucose = {0};

// Alarm state tracking (non-static so display.c can access alarm_active)
volatile bool alarm_active = false;
static volatile bool alarm_snoozed = false;
static volatile int64_t alarm_snooze_until = 0;  // Timestamp in microseconds

// Embedded alarm audio
extern const uint8_t ahs_hypo_wav_start[] asm("_binary_ahs_hypo_wav_start");
extern const uint8_t ahs_hypo_wav_end[] asm("_binary_ahs_hypo_wav_end");
extern esp_codec_dev_handle_t display_get_audio_codec(void);  // From display.c

// Forward declarations
static void on_about_next_button(void);
static void on_setup_next_button(void);
static void on_retry_button(void);
static void on_restart_setup_button(void);
static void on_reset_button(void);
static void on_about_button(void);
static void on_about_back_button(void);
static void on_configure_button(void);
static void red_button_handler(void *arg, void *data);
static void mute_button_handler(void *arg, void *data);
static void alarm_task(void *pvParameters);
static void on_ota_proceed(void);
static void on_ota_cancel(void);
static void ota_progress_callback(int progress_percent, const char *message);
static void check_for_ota_update(void);
static bool is_glucose_data_stale(const char *timestamp);

// Mute button handler - snooze alarm
static void mute_button_handler(void *arg, void *data) {
    ESP_LOGI(TAG, "MUTE BUTTON PRESSED");
    
    if (alarm_active && !alarm_snoozed) {
        // Snooze the alarm
        global_settings_t settings;
        global_settings_load(&settings);
        
        int64_t snooze_duration_us = (int64_t)settings.alarm_snooze_minutes * 60 * 1000000;
        alarm_snooze_until = esp_timer_get_time() + snooze_duration_us;
        alarm_snoozed = true;
        
        ESP_LOGI(TAG, "Alarm snoozed for %lu minutes", settings.alarm_snooze_minutes);
    }
}

// Alarm audio playback task
static void alarm_task(void *pvParameters) {
    ESP_LOGI(TAG, "Alarm task started");
    
    esp_codec_dev_handle_t codec = NULL;
    bool codec_opened = false;
    
    while (1) {
        // Check if alarm should be active
        bool should_alarm = false;
        
        if (alarm_active) {
            // Check if snoozed and snooze expired
            if (alarm_snoozed) {
                int64_t now = esp_timer_get_time();
                if (now >= alarm_snooze_until) {
                    ESP_LOGI(TAG, "Snooze expired, alarm reactivating");
                    alarm_snoozed = false;
                    should_alarm = true;
                }
            } else {
                should_alarm = true;
            }
        }
        
        if (should_alarm) {
            // Get codec handle (only once)
            if (codec == NULL) {
                codec = display_get_audio_codec();
            }
            
            if (codec != NULL) {
                size_t wav_size = ahs_hypo_wav_end - ahs_hypo_wav_start;
                
                if (wav_size > 44) {
                    // Open codec only once when alarm starts
                    if (!codec_opened) {
                        uint16_t channels = *((uint16_t*)(ahs_hypo_wav_start + 22));
                        uint32_t sample_rate = *((uint32_t*)(ahs_hypo_wav_start + 24));
                        
                        esp_codec_dev_sample_info_t fs = {
                            .sample_rate = sample_rate,
                            .channel = channels,
                            .bits_per_sample = 16,
                        };
                        
                        esp_codec_dev_close(codec);  // Close any previous state
                        vTaskDelay(pdMS_TO_TICKS(100));  // Let codec settle
                        esp_codec_dev_open(codec, &fs);
                        esp_codec_dev_set_out_vol(codec, 70);  // Lower volume to 70 to reduce distortion
                        codec_opened = true;
                        ESP_LOGI(TAG, "Alarm codec opened (sample_rate=%lu, channels=%d)", sample_rate, channels);
                    }
                    
                    const uint8_t *pcm_data = ahs_hypo_wav_start + 44;
                    size_t pcm_size = wav_size - 44;
                    
                    // Write audio in chunks with small yields to prevent blocking
                    const size_t CHUNK_SIZE = 4096;  // 4KB chunks for better responsiveness
                    size_t total_written = 0;
                    size_t offset = 0;
                    
                    while (offset < pcm_size && alarm_active && !alarm_snoozed) {
                        size_t chunk_size = (pcm_size - offset) > CHUNK_SIZE ? CHUNK_SIZE : (pcm_size - offset);
                        int bytes_written = esp_codec_dev_write(codec, (void*)(pcm_data + offset), chunk_size);
                        
                        if (bytes_written > 0) {
                            total_written += bytes_written;
                        }
                        offset += chunk_size;
                        
                        // Small yield every few chunks to let other tasks run
                        if (offset % (CHUNK_SIZE * 4) == 0) {
                            vTaskDelay(pdMS_TO_TICKS(1));
                        }
                    }
                    
                    // Loop immediately without pause for continuous playback
                } else {
                    vTaskDelay(pdMS_TO_TICKS(100));
                }
            } else {
                vTaskDelay(pdMS_TO_TICKS(100));
            }
        } else {
            // Alarm stopped - close codec if it was opened
            if (codec_opened) {
                esp_codec_dev_close(codec);
                codec_opened = false;
                ESP_LOGI(TAG, "Alarm codec closed");
            }
            // Not alarming, wait longer
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }
}

// Check if glucose timestamp is older than 5 minutes
static bool is_glucose_data_stale(const char *timestamp) {
    if (!timestamp || strcmp(timestamp, "Unknown") == 0) {
        ESP_LOGW(TAG, "Timestamp is unknown or NULL");
        return true;
    }
    
    // Check if system time is valid (after year 2020)
    time_t current_time = time(NULL);
    struct tm *current_tm = localtime(&current_time);
    if (current_tm->tm_year < 120) {  // 120 = 2020
        ESP_LOGW(TAG, "System time not synced yet (year: %d), cannot check staleness", current_tm->tm_year + 1900);
        return false;  // Don't show stale warning if we can't check
    }
    
    // Parse timestamp format: "dd/mm/yyyy HH:MM:SS"
    struct tm glucose_time = {0};
    int day, month, year, hour, minute, second;
    
    if (sscanf(timestamp, "%d/%d/%d %d:%d:%d", &day, &month, &year, &hour, &minute, &second) == 6) {
        glucose_time.tm_mday = day;
        glucose_time.tm_mon = month - 1;  // months are 0-11
        glucose_time.tm_year = year - 1900;  // years since 1900
        glucose_time.tm_hour = hour;
        glucose_time.tm_min = minute;
        glucose_time.tm_sec = second;
        glucose_time.tm_isdst = -1;  // Let mktime determine DST
        
        time_t glucose_timestamp = mktime(&glucose_time);
        
        double age_seconds = difftime(current_time, glucose_timestamp);
        ESP_LOGI(TAG, "Glucose data age: %.0f seconds (%.1f minutes)", age_seconds, age_seconds / 60.0);
        
        // Check if more than 5 minutes old (300 seconds)
        if (age_seconds > 300) {
            return true;
        }
        return false;
    }
    
    // If we couldn't parse, consider it stale
    return true;
}

// Callbacks for WiFi events
static void on_wifi_connected(void) {
    wifi_ready = true;
    const char *ssid = wifi_manager_get_ssid();
    const char *ip = wifi_manager_get_ip();
    ESP_LOGI(TAG, "WiFi Connected - SSID: %s, IP: %s", ssid, ip);
    
    // Wait for DNS to be fully operational (especially important after OTA reboots)
    // This prevents "getaddrinfo() returns 202" errors
    ESP_LOGI(TAG, "Waiting for network stack to stabilize...");
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // Initialize SNTP for time synchronization
    ESP_LOGI(TAG, "Initializing SNTP");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
    
    // Set timezone to UTC (or adjust based on your needs)
    setenv("TZ", "UTC0", 1);
    tzset();
    
    if (setup_in_progress) {
        // User is on setup screen - show Next button
        display_setup_wifi_connected();
    } else {
        // Check if LibreLink credentials exist OR demo mode is enabled
        if (DEMO_MODE_ENABLED) {
            // Demo mode - show demo glucose reading
            display_show_glucose(DEMO_GLUCOSE_MMOL, DEMO_TREND, DEMO_GLUCOSE_LOW, DEMO_GLUCOSE_HIGH, "Demo Mode", 1);
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
            display_show_glucose(DEMO_GLUCOSE_MMOL, DEMO_TREND, DEMO_GLUCOSE_LOW, DEMO_GLUCOSE_HIGH, "Demo Mode", 1);
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
                               current_glucose.is_low, current_glucose.is_high,
                               current_glucose.timestamp, current_glucose.measurement_color);
        } else if (libre_credentials_exist()) {
            // Real credentials - show current glucose if we have it, otherwise loading
            if (current_glucose.value_mmol > 0) {
                // Check if data is stale before showing
                if (is_glucose_data_stale(current_glucose.timestamp)) {
                    ESP_LOGW(TAG, "Glucose data is stale when returning from settings");
                    display_show_no_recent_data();
                } else {
                    display_show_glucose(current_glucose.value_mmol, 
                                       librelinkup_get_trend_string(current_glucose.trend), 
                                       current_glucose.is_low, current_glucose.is_high,
                                       current_glucose.timestamp, current_glucose.measurement_color);
                }
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
        display_show_settings(on_reset_button, on_about_button, on_configure_button);
    }
}

// Settings screen callbacks
static void on_about_button(void) {
    ESP_LOGI(TAG, "About button pressed");
    display_show_about_message(on_about_back_button);
}

static void on_about_back_button(void) {
    ESP_LOGI(TAG, "About back button pressed");
    display_show_settings(on_reset_button, on_about_button, on_configure_button);
}

static void on_configure_button(void) {
    ESP_LOGI(TAG, "Configure button pressed");
    display_show_configure_qr();
}

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
    
    // Delete warning screen and show progress screen
    display_ota_warning_start_update();
    vTaskDelay(pdMS_TO_TICKS(500));  // Give UI time to render
    
    // Start the update
    esp_err_t ret = ota_perform_update(ota_progress_callback);
    
    if (ret != ESP_OK) {
        // OTA failed - show error to user
        ESP_LOGE(TAG, "OTA update failed: %s", esp_err_to_name(ret));
        display_show_wifi_status("Update failed!\n\nReturning to glucose...");
        vTaskDelay(pdMS_TO_TICKS(3000));
        
        // Clear OTA flags and return to normal operation
        ota_in_progress = false;
        ota_check_complete = true;
        
        // Return to glucose screen
        if (current_glucose.value_mmol > 0) {
            if (is_glucose_data_stale(current_glucose.timestamp)) {
                display_show_no_recent_data();
            } else {
                display_show_glucose(current_glucose.value_mmol, 
                                   librelinkup_get_trend_string(current_glucose.trend),
                                   current_glucose.is_low, current_glucose.is_high,
                                   current_glucose.timestamp, current_glucose.measurement_color);
            }
        } else {
            display_show_wifi_status("Loading glucose data...");
        }
    }
    // If successful, device will reboot, so no need for else clause
}

static void on_ota_cancel(void) {
    ESP_LOGI(TAG, "User cancelled OTA update");
    ota_in_progress = false;  // Allow glucose updates again
    ota_check_complete = true;  // Signal glucose fetch can proceed
    // Return to glucose display or appropriate screen
    if (DEMO_MODE_ENABLED) {
        display_show_glucose(current_glucose.value_mmol > 0 ? current_glucose.value_mmol : 6.7, 
                           librelinkup_get_trend_string(current_glucose.trend), 
                           current_glucose.is_low, current_glucose.is_high,
                           current_glucose.timestamp, current_glucose.measurement_color);
    } else if (libre_credentials_exist()) {
        if (current_glucose.value_mmol > 0) {
            // Check if data is stale before showing
            if (is_glucose_data_stale(current_glucose.timestamp)) {
                ESP_LOGW(TAG, "Glucose data is stale when canceling OTA");
                display_show_no_recent_data();
            } else {
                display_show_glucose(current_glucose.value_mmol, 
                                   librelinkup_get_trend_string(current_glucose.trend), 
                                   current_glucose.is_low, current_glucose.is_high,
                                   current_glucose.timestamp, current_glucose.measurement_color);
            }
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
        ota_in_progress = true;  // Block glucose updates
        ota_check_complete = true;  // Check is complete, but wait for user decision
        display_show_ota_warning(on_ota_proceed, on_ota_cancel);
    } else if (ret == ESP_ERR_NOT_FOUND) {
        ESP_LOGI(TAG, "Already running latest firmware version");
        // Signal that OTA check is complete so glucose fetch can proceed
        ota_check_complete = true;
    } else {
        ESP_LOGW(TAG, "Failed to check for OTA update: %s", esp_err_to_name(ret));
        // Signal that OTA check is complete so glucose fetch can proceed
        ota_check_complete = true;
    }
}

// Task to periodically fetch glucose data from LibreLinkUp
static void glucose_fetch_task(void *pvParameters) {
#if !DEMO_MODE_ENABLED
    char email[128] = {0};
    char password[128] = {0};
    bool use_eu_server = false;
#endif
    
    bool first_fetch = true;
    
    while (1) {
        // On first iteration, wait for OTA check to complete, then fetch immediately
        if (first_fetch) {
            // Wait for OTA check to complete AND OTA to not be in progress
            while (!ota_check_complete || ota_in_progress) {
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
            ESP_LOGI(TAG, "OTA check complete, proceeding with glucose fetch");
        } else {
            // Subsequent iterations wait for the configured interval
            uint32_t interval_ms = global_settings_get_interval_ms();
            ESP_LOGI(TAG, "Next glucose update in %lu minutes", interval_ms / 60000);
            vTaskDelay(pdMS_TO_TICKS(interval_ms));
        }
        first_fetch = false;
        
        // Skip glucose updates if OTA is in progress
        if (ota_in_progress) {
            ESP_LOGI(TAG, "Skipping glucose update - OTA in progress");
            continue;
        }
        
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
                
                // Only login if we don't already have a valid token from NVS
                if (!librelinkup_is_logged_in()) {
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
                } else {
                    ESP_LOGI(TAG, "Using existing auth token from NVS");
                    libre_logged_in = true;
                    
                    // If no patient ID stored, get it now
                    if (libre_patient_id[0] == '\0') {
                        if (librelinkup_get_patient_id(libre_patient_id, sizeof(libre_patient_id)) == ESP_OK) {
                            ESP_LOGI(TAG, "Got patient ID: %s", libre_patient_id);
                            // Save it for next time
                            libre_credentials_save(email, password, libre_patient_id, use_eu_server);
                        }
                    }
                }
            }
#endif
        }
        
        // Fetch glucose data
        if (libre_logged_in && libre_patient_id[0] != '\0') {
            ESP_LOGI(TAG, "Fetching glucose data...");
            esp_err_t err = librelinkup_get_glucose(libre_patient_id, &current_glucose);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "Glucose: %d mg/dL, Trend: %s", 
                        current_glucose.value_mgdl, 
                        librelinkup_get_trend_string(current_glucose.trend));
                
                // Check for threshold violations and manage alarm
                global_settings_t settings;
                global_settings_load(&settings);
                
                // Calculate thresholds locally (don't trust API's isLow/isHigh flags)
                bool is_low_calculated = current_glucose.value_mmol < settings.glucose_low_threshold;
                bool is_high_calculated = current_glucose.value_mmol > settings.glucose_high_threshold;
                
                // Check if alarm should be triggered based on individual low/high settings
                bool should_alarm = settings.alarm_enabled && 
                                   ((is_low_calculated && settings.alarm_low_enabled) || 
                                    (is_high_calculated && settings.alarm_high_enabled));
                
                if (should_alarm) {
                    // Only activate alarm if not already active (don't reset snooze state on glucose refresh)
                    if (!alarm_active) {
                        // Start alarm
                        ESP_LOGW(TAG, "THRESHOLD VIOLATED - Starting alarm! (Low: %d, High: %d, Value: %.1f mmol/L)",
                                 is_low_calculated, is_high_calculated, current_glucose.value_mmol);
                        alarm_active = true;
                        alarm_snoozed = false;
                    } else {
                        ESP_LOGD(TAG, "Threshold still violated, alarm continues (active: %d, snoozed: %d)", 
                                 alarm_active, alarm_snoozed);
                    }
                } else {
                    // Glucose back in range or alarm disabled - stop alarm
                    if (alarm_active) {
                        ESP_LOGI(TAG, "Glucose back in range - Stopping alarm");
                        alarm_active = false;
                        alarm_snoozed = false;
                    }
                }
                
                // Update display if not in settings
                if (!settings_shown && !setup_in_progress) {
                    // Check if data is stale (older than 5 minutes)
                    if (is_glucose_data_stale(current_glucose.timestamp)) {
                        ESP_LOGW(TAG, "Glucose data is stale (older than 5 minutes): %s", current_glucose.timestamp);
                        display_show_no_recent_data();
                    } else {
                        display_show_glucose(current_glucose.value_mmol, 
                                           librelinkup_get_trend_string(current_glucose.trend),
                                           current_glucose.is_low, current_glucose.is_high,
                                           current_glucose.timestamp, current_glucose.measurement_color);
                    }
                }
            } else if (err == ESP_ERR_LIBRE_AUTH_FAILED) {
                // Only force re-login on actual authentication failures (401)
                ESP_LOGE(TAG, "Authentication failed - forcing re-login");
                libre_logged_in = false;
                // Show error on display if not in settings
                if (!settings_shown && !setup_in_progress) {
                    display_show_wifi_status("Auth failed\nRetrying...");
                }
            } else if (err == ESP_ERR_LIBRE_RATE_LIMITED) {
                // Rate limited - just log and wait, don't re-login!
                ESP_LOGW(TAG, "Rate limited - will retry on next cycle");
                // Show error on display if not in settings
                if (!settings_shown && !setup_in_progress) {
                    display_show_wifi_status("Rate limited\nWaiting...");
                }
            } else {
                ESP_LOGE(TAG, "Failed to fetch glucose data: %s", esp_err_to_name(err));
                // Show error on display if not in settings
                if (!settings_shown && !setup_in_progress) {
                    display_show_wifi_status("Fetch failed\nRetrying...");
                }
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
        
        // Register mute button callback for alarm snooze
        if (btn_cnt > BSP_BUTTON_MUTE && btns[BSP_BUTTON_MUTE] != NULL) {
            ESP_LOGI(TAG, "Attempting to register callback for BSP_BUTTON_MUTE (index %d, handle %p)", 
                     BSP_BUTTON_MUTE, btns[BSP_BUTTON_MUTE]);
            
            esp_err_t mute_err = iot_button_register_cb(btns[BSP_BUTTON_MUTE], BUTTON_SINGLE_CLICK, mute_button_handler, NULL);
            ESP_LOGI(TAG, "MUTE button registration: %s", esp_err_to_name(mute_err));
        } else {
            ESP_LOGW(TAG, "Mute button not available! btn_cnt=%d, BSP_BUTTON_MUTE=%d", btn_cnt, BSP_BUTTON_MUTE);
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
    
    // Start alarm audio task (higher priority for smooth audio)
    xTaskCreate(alarm_task, "alarm_task", 4096, NULL, 6, NULL);
    ESP_LOGI(TAG, "Alarm task created");
    
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
        // Signal OTA check complete (won't run without WiFi)
        ota_check_complete = true;
        // Don't return - let the task continue running
    } else {
        // No credentials - AP mode already started, show About screen
        ESP_LOGI(TAG, "No WiFi credentials, AP mode active, showing About screen");
        display_show_about(on_about_next_button);
        // Signal OTA check complete (won't run without WiFi)
        ota_check_complete = true;
    }
    
    ESP_LOGI(TAG, "Initialization complete");
    ESP_LOGI(TAG, "========================================\n");
}
