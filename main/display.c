#include "display.h"
#include "config.h"
#include "wifi_manager.h"
#include "ir_transmitter.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bsp/esp-bsp.h"
#include <string.h>

// Embedded PNG image
extern const uint8_t supreme_glucose_splash_png_start[] asm("_binary_supreme_glucose_splash_png_start");
extern const uint8_t supreme_glucose_splash_png_end[] asm("_binary_supreme_glucose_splash_png_end");

static const char *TAG = "DISPLAY";

// Current screen tracking
static lv_obj_t *current_screen = NULL;

// Setup screen elements for dynamic updates
static lv_obj_t *setup_spinner = NULL;
static lv_obj_t *setup_next_btn = NULL;

esp_err_t display_init(void)
{
    ESP_LOGI(TAG, "Initializing display with BSP...");
    
    // Initialize I2C (required by BSP for touch, audio, etc.)
    bsp_i2c_init();
    
    // Initialize display using BSP - this handles everything!
    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_H_RES * 50,  // 50 lines buffer
        .double_buffer = 0,
        .flags = {
            .buff_dma = true,
        }
    };
    bsp_display_start_with_config(&cfg);
    
    // Turn on backlight
    bsp_display_backlight_on();
    
    ESP_LOGI(TAG, "Display initialized successfully via BSP");
    
    // Create display task
    xTaskCreate(display_task, "display_task", 8192, NULL, 5, NULL);
    
    return ESP_OK;
}

void display_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Display task started");
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void display_lock(void)
{
    bsp_display_lock(0);
}

void display_unlock(void)
{
    bsp_display_unlock();
}

void display_show_splash(void)
{
    display_lock();
    
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    
    // Disable scrollbars
    lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);
    
    // Create a witchy symbol (star/pentagram)
    lv_obj_t *symbol_label = lv_label_create(screen);
    lv_label_set_text(symbol_label, "*");  // Star symbol
    lv_obj_set_style_text_color(symbol_label, lv_color_make(150, 0, 200), 0);  // Purple/mystical color
    lv_obj_set_style_text_font(symbol_label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_align(symbol_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_transform_scale(symbol_label, 600, 0);  // Large but not too large
    lv_obj_align(symbol_label, LV_ALIGN_CENTER, 0, -30);  // Slightly above center
    
    // Add device name text below
    lv_obj_t *text_label = lv_label_create(screen);
    lv_label_set_text(text_label, DEVICE_NAME);
    lv_obj_set_style_text_color(text_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(text_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_align(text_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(text_label, LV_ALIGN_BOTTOM_MID, 0, -30);
    
    lv_screen_load(screen);
    current_screen = screen;
    
    display_unlock();
    
    ESP_LOGI(TAG, "Splash screen displayed: Supreme Glucose PNG image");
}

void display_show_about(display_button_callback_t callback)
{
    display_lock();
    
    if (current_screen) {
        lv_obj_del(current_screen);
    }
    
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    
    // About text (no title header)
    lv_obj_t *label = lv_label_create(screen);
    lv_label_set_text(label, 
        "This device is intended for\n"
        "the Supreme (Stephen Higgins).\n"
        "It's a glucose monitor that\n"
        "uses data from LibreLink.\n"
        "Paired with the Moon Lamp,\n"
        "it will set the color of the\n"
        "moon dependent on the current\n"
        "glucose levels.\n\n"
        "With love, Spalding");
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, -20);
    
    // Create Next button
    lv_obj_t *btn = lv_btn_create(screen);
    lv_obj_set_size(btn, 100, 40);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -20);
    
    lv_obj_t *btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "Next");
    lv_obj_center(btn_label);
    
    if (callback) {
        lv_obj_add_event_cb(btn, (lv_event_cb_t)callback, LV_EVENT_CLICKED, NULL);
    }
    
    lv_screen_load(screen);
    current_screen = screen;
    
    display_unlock();
    
    ESP_LOGI(TAG, "About screen displayed");
}

void display_show_setup(display_button_callback_t callback)
{
    display_lock();
    
    if (current_screen) {
        lv_obj_del(current_screen);
    }
    
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    
    // Instructions text
    lv_obj_t *label = lv_label_create(screen);
    
    char instructions[256];
    snprintf(instructions, sizeof(instructions),
        "This device will need to\n"
        "connect to the internet to\n"
        "function. On your phone,\n"
        "connect to the below WiFi\n"
        "where you can enter your own\n"
        "home WiFi information.\n\n"
        "WiFi: %s\n"
        "Password: %s",
        WIFI_AP_SSID, WIFI_AP_PASSWORD);
    
    lv_label_set_text(label, instructions);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, -30);
    
    // Create spinner (initially visible)
    setup_spinner = lv_spinner_create(screen);
    lv_obj_set_size(setup_spinner, 40, 40);
    lv_obj_align(setup_spinner, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_set_style_arc_color(setup_spinner, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_arc_color(setup_spinner, lv_color_hex(0x00FF00), LV_PART_INDICATOR);
    
    // Create Next button (initially hidden)
    setup_next_btn = lv_btn_create(screen);
    lv_obj_set_size(setup_next_btn, 100, 40);
    lv_obj_align(setup_next_btn, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_add_flag(setup_next_btn, LV_OBJ_FLAG_HIDDEN);  // Hide initially
    
    lv_obj_t *btn_label = lv_label_create(setup_next_btn);
    lv_label_set_text(btn_label, "Next");
    lv_obj_center(btn_label);
    
    if (callback) {
        lv_obj_add_event_cb(setup_next_btn, (lv_event_cb_t)callback, LV_EVENT_CLICKED, NULL);
    }
    
    lv_screen_load(screen);
    current_screen = screen;
    
    display_unlock();
    
    ESP_LOGI(TAG, "Setup screen displayed with spinner");
}

void display_show_wifi_status(const char *status)
{
    display_lock();
    
    if (current_screen) {
        lv_obj_del(current_screen);
    }
    
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    
    lv_obj_t *label = lv_label_create(screen);
    lv_label_set_text(label, status);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_center(label);
    
    lv_screen_load(screen);
    current_screen = screen;
    
    display_unlock();
    
    ESP_LOGI(TAG, "WiFi status displayed: %s", status);
}

void display_setup_wifi_connected(void)
{
    display_lock();
    
    // Hide spinner and show Next button
    if (setup_spinner != NULL) {
        lv_obj_add_flag(setup_spinner, LV_OBJ_FLAG_HIDDEN);
    }
    
    if (setup_next_btn != NULL) {
        lv_obj_clear_flag(setup_next_btn, LV_OBJ_FLAG_HIDDEN);
    }
    
    display_unlock();
    
    ESP_LOGI(TAG, "Setup screen updated: WiFi connected, Next button shown");
}

// Static glucose screen elements for flashing animation
static lv_obj_t *glucose_screen = NULL;
static lv_timer_t *flash_timer = NULL;
static bool flash_state = false;

// Timer callback for flashing background
static void flash_timer_cb(lv_timer_t *timer) {
    if (glucose_screen == NULL) {
        return;
    }
    
    flash_state = !flash_state;
    if (flash_state) {
        lv_obj_set_style_bg_color(glucose_screen, lv_color_make(180, 0, 0), 0); // Dark red
    } else {
        lv_obj_set_style_bg_color(glucose_screen, lv_color_make(255, 0, 0), 0); // Bright red
    }
}

void display_show_glucose(float glucose_mmol, const char *trend, bool is_low, bool is_high)
{
    display_lock();
    
    // Stop any existing flash timer
    if (flash_timer != NULL) {
        lv_timer_del(flash_timer);
        flash_timer = NULL;
    }
    
    if (current_screen) {
        lv_obj_del(current_screen);
    }
    
    lv_obj_t *screen = lv_obj_create(NULL);
    glucose_screen = screen;
    
    // Set background color based on glucose level
    if (is_low && is_high) {
        // Critical - both high and low flags (error state) - flash red
        lv_obj_set_style_bg_color(screen, lv_color_make(255, 0, 0), 0);
        flash_timer = lv_timer_create(flash_timer_cb, 500, NULL); // Flash every 500ms
    } else if (is_low) {
        // Low glucose - red background
        lv_obj_set_style_bg_color(screen, lv_color_make(255, 0, 0), 0);
    } else if (is_high) {
        // High glucose - yellow background
        lv_obj_set_style_bg_color(screen, lv_color_make(200, 150, 0), 0);
    } else {
        // Normal range - green background
        lv_obj_set_style_bg_color(screen, lv_color_make(0, 150, 0), 0);
    }
    
    // Huge glucose number - fill most of the screen
    lv_obj_t *glucose_label = lv_label_create(screen);
    char glucose_text[32];
    snprintf(glucose_text, sizeof(glucose_text), "%.1f", glucose_mmol);  // Just the number
    lv_label_set_text(glucose_label, glucose_text);
    lv_obj_set_style_text_color(glucose_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(glucose_label, &lv_font_montserrat_48, 0);  // Use 48pt font
    lv_obj_set_style_text_align(glucose_label, LV_TEXT_ALIGN_CENTER, 0);  // Center align text
    lv_obj_set_style_transform_scale(glucose_label, 400, 0);  // Scale up 4x - crisp and huge
    // Position above center to account for scaling
    lv_obj_align(glucose_label, LV_ALIGN_CENTER, 0, -40);
    
    // Trend symbol to the left of glucose number
    lv_obj_t *trend_label = lv_label_create(screen);
    const char *trend_symbol;
    
    // Map trend string to LVGL symbols
    if (strcmp(trend, "↑↑") == 0 || strcmp(trend, "^^") == 0) {
        trend_symbol = LV_SYMBOL_UP LV_SYMBOL_UP;  // Double up
    } else if (strcmp(trend, "↑") == 0 || strcmp(trend, "^") == 0) {
        trend_symbol = LV_SYMBOL_UP;
    } else if (strcmp(trend, "↓") == 0 || strcmp(trend, "v") == 0) {
        trend_symbol = LV_SYMBOL_DOWN;
    } else if (strcmp(trend, "↓↓") == 0 || strcmp(trend, "vv") == 0) {
        trend_symbol = LV_SYMBOL_DOWN LV_SYMBOL_DOWN;  // Double down
    } else {
        // Default to stable/right arrow for unknown, stable, or no data (*, →, -, ?)
        trend_symbol = LV_SYMBOL_RIGHT;
    }
    
    lv_label_set_text(trend_label, trend_symbol);
    lv_obj_set_style_text_color(trend_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(trend_label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_align(trend_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_transform_scale(trend_label, 400, 0);  // Same scale as glucose
    // Position to the left of glucose number
    lv_obj_align_to(trend_label, glucose_label, LV_ALIGN_OUT_LEFT_MID, -20, 0);
    
    // Status text at bottom
    lv_obj_t *status_label = lv_label_create(screen);
    const char *status_text;
    if (is_low && is_high) {
        status_text = "CRITICAL ERROR";
    } else if (is_low) {
        status_text = "LOW";
    } else if (is_high) {
        status_text = "HIGH";
    } else {
        status_text = "NORMAL";
    }
    lv_label_set_text(status_label, status_text);
    lv_obj_set_style_text_color(status_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_CENTER, 0);  // Center align text
    lv_obj_align(status_label, LV_ALIGN_BOTTOM_MID, 0, -20);
    
    lv_screen_load(screen);
    current_screen = screen;
    
    display_unlock();
    
    // Send IR command to Moon Lamp if enabled
    bool is_normal = !is_low && !is_high;
    ir_transmitter_set_moon_lamp_color(is_low, is_high, is_normal);
    
    ESP_LOGI(TAG, "Glucose screen displayed: %.1f mmol/L %s (Low: %d, High: %d)", 
             glucose_mmol, trend, is_low, is_high);
}

// Static callbacks for connection failed screen
static display_button_callback_t retry_callback = NULL;
static display_button_callback_t setup_callback = NULL;

static void retry_button_event(lv_event_t *e) {
    if (retry_callback) {
        retry_callback();
    }
}

static void setup_button_event(lv_event_t *e) {
    if (setup_callback) {
        setup_callback();
    }
}

void display_show_connection_failed(display_button_callback_t retry_cb, display_button_callback_t setup_cb)
{
    retry_callback = retry_cb;
    setup_callback = setup_cb;
    
    display_lock();
    
    if (current_screen) {
        lv_obj_del(current_screen);
    }
    
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    
    // Title
    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "WiFi Connection Failed");
    lv_obj_set_style_text_color(title, lv_color_make(255, 100, 100), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);
    
    // Message
    lv_obj_t *msg = lv_label_create(screen);
    lv_label_set_text(msg, "The device could not connect\nto the saved WiFi.\n\nWould you like to retry or\nrestart setup?");
    lv_obj_set_style_text_color(msg, lv_color_white(), 0);
    lv_obj_set_style_text_font(msg, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(msg, LV_ALIGN_CENTER, 0, -20);
    
    // Retry button
    lv_obj_t *retry_btn = lv_btn_create(screen);
    lv_obj_set_size(retry_btn, 120, 50);
    lv_obj_align(retry_btn, LV_ALIGN_BOTTOM_LEFT, 30, -20);
    lv_obj_set_style_bg_color(retry_btn, lv_color_make(76, 175, 80), 0);
    lv_obj_add_event_cb(retry_btn, retry_button_event, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *retry_label = lv_label_create(retry_btn);
    lv_label_set_text(retry_label, "Retry");
    lv_obj_set_style_text_font(retry_label, &lv_font_montserrat_18, 0);
    lv_obj_center(retry_label);
    
    // Setup button
    lv_obj_t *setup_btn = lv_btn_create(screen);
    lv_obj_set_size(setup_btn, 120, 50);
    lv_obj_align(setup_btn, LV_ALIGN_BOTTOM_RIGHT, -30, -20);
    lv_obj_set_style_bg_color(setup_btn, lv_color_make(100, 100, 255), 0);
    lv_obj_add_event_cb(setup_btn, setup_button_event, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *setup_label = lv_label_create(setup_btn);
    lv_label_set_text(setup_label, "Setup");
    lv_obj_set_style_text_font(setup_label, &lv_font_montserrat_18, 0);
    lv_obj_center(setup_label);
    
    lv_screen_load(screen);
    current_screen = screen;
    
    display_unlock();
    
    ESP_LOGI(TAG, "Connection failed screen displayed");
}

// Static callback for settings screen
static display_button_callback_t reset_callback = NULL;

static void reset_button_event(lv_event_t *e) {
    if (reset_callback) {
        reset_callback();
    }
}

void display_show_settings(display_button_callback_t reset_cb)
{
    reset_callback = reset_cb;
    
    display_lock();
    
    if (current_screen) {
        lv_obj_del(current_screen);
    }
    
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    
    // Title
    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "Settings");
    lv_obj_set_style_text_color(title, lv_color_make(76, 175, 80), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);
    
    // Configuration URL text
    lv_obj_t *config_url = lv_label_create(screen);
    const char *ip = wifi_manager_get_ip();
    char url_text[128];
    snprintf(url_text, sizeof(url_text), "Go to http://%s\nto configure the settings\nof this device", ip);
    lv_label_set_text(config_url, url_text);
    lv_obj_set_style_text_color(config_url, lv_color_make(150, 150, 150), 0);
    lv_obj_set_style_text_font(config_url, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(config_url, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(config_url, LV_ALIGN_CENTER, 0, -50);
    
    // Reset button
    lv_obj_t *reset_btn = lv_btn_create(screen);
    lv_obj_set_size(reset_btn, 200, 50);
    lv_obj_align(reset_btn, LV_ALIGN_CENTER, 0, 30);
    lv_obj_set_style_bg_color(reset_btn, lv_color_make(255, 100, 100), 0);
    lv_obj_add_event_cb(reset_btn, reset_button_event, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *reset_label = lv_label_create(reset_btn);
    lv_label_set_text(reset_label, "Reset Device");
    lv_obj_set_style_text_font(reset_label, &lv_font_montserrat_18, 0);
    lv_obj_center(reset_label);
    
    // Info text
    lv_obj_t *info = lv_label_create(screen);
    lv_label_set_text(info, "This will clear all WiFi\ncredentials and restart");
    lv_obj_set_style_text_color(info, lv_color_make(150, 150, 150), 0);
    lv_obj_set_style_text_font(info, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(info, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(info, LV_ALIGN_BOTTOM_MID, 0, -30);
    
    lv_screen_load(screen);
    current_screen = screen;
    
    display_unlock();
    
    ESP_LOGI(TAG, "Settings screen displayed");
}

// Static variables for OTA progress screen
static lv_obj_t *ota_bar = NULL;
static lv_obj_t *ota_percent_label = NULL;
static lv_obj_t *ota_message_label = NULL;

void display_show_ota_progress(int progress_percent, const char *message)
{
    display_lock();
    
    // Create screen on first call
    if (ota_bar == NULL) {
        lv_obj_t *screen = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
        lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);
        
        // Title
        lv_obj_t *title = lv_label_create(screen);
        lv_label_set_text(title, "Firmware Update");
        lv_obj_set_style_text_color(title, lv_color_white(), 0);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);
        
        // Warning text
        lv_obj_t *warning = lv_label_create(screen);
        lv_label_set_text(warning, "DO NOT DISCONNECT POWER!");
        lv_obj_set_style_text_color(warning, lv_color_make(255, 100, 100), 0);
        lv_obj_set_style_text_font(warning, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_align(warning, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(warning, LV_ALIGN_TOP_MID, 0, 60);
        
        // Progress bar
        ota_bar = lv_bar_create(screen);
        lv_obj_set_size(ota_bar, 260, 30);
        lv_obj_set_style_bg_color(ota_bar, lv_color_make(40, 40, 40), 0);
        lv_obj_set_style_bg_color(ota_bar, lv_color_make(0, 150, 255), LV_PART_INDICATOR);
        lv_obj_align(ota_bar, LV_ALIGN_CENTER, 0, 0);
        lv_bar_set_value(ota_bar, 0, LV_ANIM_OFF);
        
        // Percentage label
        ota_percent_label = lv_label_create(screen);
        lv_label_set_text(ota_percent_label, "0%");
        lv_obj_set_style_text_color(ota_percent_label, lv_color_white(), 0);
        lv_obj_set_style_text_font(ota_percent_label, &lv_font_montserrat_18, 0);
        lv_obj_align(ota_percent_label, LV_ALIGN_CENTER, 0, 40);
        
        // Status message
        ota_message_label = lv_label_create(screen);
        lv_label_set_text(ota_message_label, "Initializing...");
        lv_obj_set_style_text_color(ota_message_label, lv_color_make(200, 200, 200), 0);
        lv_obj_set_style_text_font(ota_message_label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_align(ota_message_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(ota_message_label, LV_ALIGN_BOTTOM_MID, 0, -40);
        
        lv_screen_load(screen);
        current_screen = screen;
    }
    
    // Update progress
    if (ota_bar) {
        lv_bar_set_value(ota_bar, progress_percent, LV_ANIM_ON);
    }
    
    // Update percentage text
    if (ota_percent_label) {
        char percent_text[8];
        snprintf(percent_text, sizeof(percent_text), "%d%%", progress_percent);
        lv_label_set_text(ota_percent_label, percent_text);
    }
    
    // Update message
    if (ota_message_label && message) {
        lv_label_set_text(ota_message_label, message);
    }
    
    display_unlock();
}

// Static storage for OTA warning callbacks
static display_button_callback_t saved_proceed_cb = NULL;
static display_button_callback_t saved_cancel_cb = NULL;

static void ota_warning_proceed_event(lv_event_t *e) {
    if (saved_proceed_cb) saved_proceed_cb();
}

static void ota_warning_cancel_event(lv_event_t *e) {
    if (saved_cancel_cb) saved_cancel_cb();
}

void display_show_ota_warning(display_button_callback_t proceed_cb, display_button_callback_t cancel_cb)
{
    display_lock();
    
    saved_proceed_cb = proceed_cb;
    saved_cancel_cb = cancel_cb;
    
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);
    
    // Title
    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "Firmware Update Available");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);
    
    // Warning box
    lv_obj_t *warning_box = lv_obj_create(screen);
    lv_obj_set_size(warning_box, 280, 120);
    lv_obj_set_style_bg_color(warning_box, lv_color_make(60, 20, 20), 0);
    lv_obj_set_style_border_color(warning_box, lv_color_make(255, 100, 100), 0);
    lv_obj_set_style_border_width(warning_box, 2, 0);
    lv_obj_align(warning_box, LV_ALIGN_CENTER, 0, -10);
    
    lv_obj_t *warning_text = lv_label_create(warning_box);
    lv_label_set_text(warning_text, 
        "WARNING!\n\n"
        "Do NOT disconnect power\n"
        "during the update process.\n"
        "Device will reboot when\n"
        "update is complete.");
    lv_obj_set_style_text_color(warning_text, lv_color_white(), 0);
    lv_obj_set_style_text_font(warning_text, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(warning_text, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(warning_text, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(warning_text, 260);
    lv_obj_center(warning_text);
    
    // Proceed button
    static lv_obj_t *proceed_btn = NULL;
    proceed_btn = lv_btn_create(screen);
    lv_obj_set_size(proceed_btn, 130, 45);
    lv_obj_align(proceed_btn, LV_ALIGN_BOTTOM_LEFT, 20, -20);
    lv_obj_set_style_bg_color(proceed_btn, lv_color_make(0, 150, 0), 0);
    lv_obj_add_event_cb(proceed_btn, ota_warning_proceed_event, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *proceed_label = lv_label_create(proceed_btn);
    lv_label_set_text(proceed_label, "Update Now");
    lv_obj_set_style_text_font(proceed_label, &lv_font_montserrat_14, 0);
    lv_obj_center(proceed_label);
    
    // Cancel button
    static lv_obj_t *cancel_btn = NULL;
    cancel_btn = lv_btn_create(screen);
    lv_obj_set_size(cancel_btn, 130, 45);
    lv_obj_align(cancel_btn, LV_ALIGN_BOTTOM_RIGHT, -20, -20);
    lv_obj_set_style_bg_color(cancel_btn, lv_color_make(100, 100, 100), 0);
    lv_obj_add_event_cb(cancel_btn, ota_warning_cancel_event, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *cancel_label = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_label, "Later");
    lv_obj_set_style_text_font(cancel_label, &lv_font_montserrat_14, 0);
    lv_obj_center(cancel_label);
    
    lv_screen_load(screen);
    current_screen = screen;
    
    display_unlock();
    
    ESP_LOGI(TAG, "OTA warning screen displayed");
}
