#include "display.h"
#include "config.h"
#include "wifi_manager.h"
#include "ir_transmitter.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bsp/esp-bsp.h"
#include "esp_codec_dev.h"
#include "driver/gpio.h"
#include "cJSON.h"
#include <string.h>
#include <time.h>
#include <math.h>
#include <stdlib.h>

// Speaker power amplifier GPIO (GPIO 46)
#define SPEAKER_PWR_GPIO GPIO_NUM_46

// Embedded PNG image
extern const uint8_t supreme_glucose_splash_png_start[] asm("_binary_supreme_glucose_splash_png_start");
extern const uint8_t supreme_glucose_splash_png_end[] asm("_binary_supreme_glucose_splash_png_end");

// Embedded WAV audio file
extern const uint8_t ahs_lala_wav_start[] asm("_binary_ahs_lala_wav_start");
extern const uint8_t ahs_lala_wav_end[] asm("_binary_ahs_lala_wav_end");

// Embedded JSON quotes file
extern const uint8_t random_quotes_json_start[] asm("_binary_random_quotes_json_start");
extern const uint8_t random_quotes_json_end[] asm("_binary_random_quotes_json_end");

static const char *TAG = "DISPLAY";

// Audio codec handle for speaker
static esp_codec_dev_handle_t spk_codec_dev = NULL;

// Current screen tracking
static lv_obj_t *current_screen = NULL;

// Setup screen elements for dynamic updates
static lv_obj_t *setup_spinner = NULL;
static lv_obj_t *setup_next_btn = NULL;

// Triple tap detection for surprise screen
static int tap_count = 0;
static uint32_t last_tap_time = 0;

// Last glucose values for restoring screen after surprise
static float last_glucose_mmol = 0.0f;
static char last_trend[8] = "*";
static bool last_is_low = false;
static bool last_is_high = false;
static char last_timestamp[32] = "Unknown";
static int last_measurement_color = 1;

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

// Tap event to dismiss surprise screen
static void surprise_screen_tap_event(lv_event_t *e)
{
    ESP_LOGI(TAG, "Surprise screen dismissed");
    // Restore the last glucose screen
    display_show_glucose(last_glucose_mmol, last_trend, last_is_low, last_is_high, last_timestamp, last_measurement_color);
}

// Hidden surprise screen
static void display_show_surprise(void)
{
    display_lock();
    
    if (current_screen) {
        lv_obj_del(current_screen);
    }
    
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_make(75, 0, 130), 0);  // Deep purple/indigo
    
    // Big "Surprise, bitch!" text
    lv_obj_t *surprise_label = lv_label_create(screen);
    lv_label_set_text(surprise_label, "Surprise,\nbitch!");
    lv_obj_set_style_text_color(surprise_label, lv_color_make(255, 215, 0), 0);  // Gold text
    lv_obj_set_style_text_font(surprise_label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_align(surprise_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(surprise_label, LV_ALIGN_CENTER, 0, 0);
    
    // Witchy emoji/symbol
    lv_obj_t *witch_label = lv_label_create(screen);
    lv_label_set_text(witch_label, "* * *");  // Stars instead of emojis
    lv_obj_set_style_text_color(witch_label, lv_color_make(255, 215, 0), 0);
    lv_obj_set_style_text_font(witch_label, &lv_font_montserrat_18, 0);
    lv_obj_align(witch_label, LV_ALIGN_TOP_MID, 0, 20);
    
    // Tap instruction at bottom
    lv_obj_t *tap_hint = lv_label_create(screen);
    lv_label_set_text(tap_hint, "Tap to dismiss");
    lv_obj_set_style_text_color(tap_hint, lv_color_make(255, 215, 0), 0);
    lv_obj_set_style_text_font(tap_hint, &lv_font_montserrat_14, 0);
    lv_obj_align(tap_hint, LV_ALIGN_BOTTOM_MID, 0, -20);
    
    // Add tap event to dismiss
    lv_obj_add_event_cb(screen, surprise_screen_tap_event, LV_EVENT_CLICKED, NULL);
    
    lv_screen_load(screen);
    current_screen = screen;
    
    display_unlock();
    
    ESP_LOGI(TAG, "🔮 Surprise screen activated!");
    
    // Small delay to ensure screen is visible before audio
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Play surprise audio
    if (spk_codec_dev != NULL) {
        extern const uint8_t ahs_surprise_wav_start[] asm("_binary_ahs_surprise_wav_start");
        extern const uint8_t ahs_surprise_wav_end[] asm("_binary_ahs_surprise_wav_end");
        
        size_t wav_size = ahs_surprise_wav_end - ahs_surprise_wav_start;
        ESP_LOGI(TAG, "Playing surprise audio (%d bytes)", wav_size);
        
        // Parse WAV header: sample rate (offset 24-27), channels (offset 22-23)
        if (wav_size > 44) {
            uint16_t channels = *((uint16_t*)(ahs_surprise_wav_start + 22));
            uint32_t sample_rate = *((uint32_t*)(ahs_surprise_wav_start + 24));
            ESP_LOGI(TAG, "WAV: %lu Hz, %d channel(s)", sample_rate, channels);
            
            esp_codec_dev_sample_info_t fs = {
                .sample_rate = sample_rate,
                .channel = channels,
                .bits_per_sample = 16,
            };
            
            esp_codec_dev_close(spk_codec_dev);
            esp_codec_dev_open(spk_codec_dev, &fs);
            esp_codec_dev_set_out_vol(spk_codec_dev, 75);
            
            const uint8_t *pcm_data = ahs_surprise_wav_start + 44;
            size_t pcm_size = wav_size - 44;
            
            int bytes_written = esp_codec_dev_write(spk_codec_dev, (void*)pcm_data, pcm_size);
            ESP_LOGI(TAG, "Wrote %d bytes of PCM data", bytes_written);
        }
    }
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
    
    // Play splash audio
    if (spk_codec_dev == NULL) {
        // Initialize audio on first use
        ESP_LOGI(TAG, "Initializing audio for splash screen...");
        
        // Configure speaker power GPIO (GPIO 46) - powers the amplifier
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << SPEAKER_PWR_GPIO),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        
        esp_err_t ret = gpio_config(&io_conf);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to configure speaker power GPIO%d: %s", SPEAKER_PWR_GPIO, esp_err_to_name(ret));
        }
        
        // Set GPIO46 to HIGH to enable speaker amplifier power
        gpio_set_level(SPEAKER_PWR_GPIO, 1);
        ESP_LOGI(TAG, "GPIO%d set to HIGH (speaker amplifier powered ON)", SPEAKER_PWR_GPIO);
        
        // Small delay to let amplifier power stabilize
        vTaskDelay(pdMS_TO_TICKS(50));
        
        // Initialize I2S for audio (pass NULL to use default configuration)
        ret = bsp_audio_init(NULL);
        if (ret == ESP_OK) {
            spk_codec_dev = bsp_audio_codec_speaker_init();
            if (spk_codec_dev != NULL) {
                ESP_LOGI(TAG, "Audio codec initialized successfully");
                esp_codec_dev_set_out_vol(spk_codec_dev, 80);  // Set volume to 80%
            } else {
                ESP_LOGE(TAG, "Failed to initialize speaker codec");
            }
        } else {
            ESP_LOGE(TAG, "Failed to initialize audio I2S: %s", esp_err_to_name(ret));
        }
    }
    
    // Play WAV audio if codec is ready
    if (spk_codec_dev != NULL) {
        ESP_LOGI(TAG, "Playing splash audio (WAV)...");
        size_t wav_size = ahs_lala_wav_end - ahs_lala_wav_start;
        ESP_LOGI(TAG, "WAV file size: %d bytes", wav_size);
        
        // Parse WAV header: sample rate (offset 24-27), channels (offset 22-23)
        if (wav_size > 44) {
            uint16_t channels = *((uint16_t*)(ahs_lala_wav_start + 22));
            uint32_t sample_rate = *((uint32_t*)(ahs_lala_wav_start + 24));
            ESP_LOGI(TAG, "WAV: %lu Hz, %d channel(s)", sample_rate, channels);
            
            esp_codec_dev_sample_info_t fs = {
                .sample_rate = sample_rate,
                .channel = channels,
                .bits_per_sample = 16,
            };
            
            esp_codec_dev_close(spk_codec_dev);
            esp_codec_dev_open(spk_codec_dev, &fs);
            esp_codec_dev_set_out_vol(spk_codec_dev, 75);
            
            const uint8_t *pcm_data = ahs_lala_wav_start + 44;
            size_t pcm_size = wav_size - 44;
            
            int bytes_written = esp_codec_dev_write(spk_codec_dev, (void*)pcm_data, pcm_size);
            ESP_LOGI(TAG, "Wrote %d bytes of PCM data", bytes_written);
        }
    }
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

// Moon phase calculation
static float calculate_moon_age(void) {
    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    
    int year = timeinfo.tm_year + 1900;
    int month = timeinfo.tm_mon + 1;
    int day = timeinfo.tm_mday;
    
    // Calculate days since known new moon (Jan 6, 2000)
    // Using simplified Julian date calculation
    int a = (14 - month) / 12;
    int y = year - a;
    int m = month + 12 * a - 3;
    
    // Julian day number
    long jdn = day + (153 * m + 2) / 5 + 365 * y + y / 4 - y / 100 + y / 400 - 32045;
    
    // Days since Jan 6, 2000 (JD 2451550)
    long daysSince = jdn - 2451550;
    
    // Moon age in days (lunar cycle is ~29.53 days)
    float moonAge = fmodf((float)daysSince, 29.53f);
    if (moonAge < 0) moonAge += 29.53f;
    
    return moonAge;
}

static const char* get_moon_phase_name(float age) {
    float normalizedPhase = age / 29.53f;
    
    if (normalizedPhase < 0.03f || normalizedPhase > 0.97f) return "New Moon";
    else if (normalizedPhase < 0.22f) return "Waxing Crescent";
    else if (normalizedPhase >= 0.22f && normalizedPhase < 0.28f) return "First Quarter";
    else if (normalizedPhase >= 0.28f && normalizedPhase < 0.47f) return "Waxing Gibbous";
    else if (normalizedPhase >= 0.47f && normalizedPhase < 0.53f) return "Full Moon";
    else if (normalizedPhase >= 0.53f && normalizedPhase < 0.72f) return "Waning Gibbous";
    else if (normalizedPhase >= 0.72f && normalizedPhase < 0.78f) return "Last Quarter";
    else return "Waning Crescent";
}

// Structure to hold quote data
typedef struct {
    char quote[256];
    char character[64];
    char episode[64];
} quote_data_t;

// Get random quote from JSON file
static bool get_random_quote(quote_data_t *quote_data) {
    // Parse the embedded JSON file
    const char* json_str = (const char*)random_quotes_json_start;
    size_t json_len = random_quotes_json_end - random_quotes_json_start;
    
    cJSON *json = cJSON_ParseWithLength(json_str, json_len);
    if (json == NULL) {
        ESP_LOGE(TAG, "Failed to parse quotes JSON");
        return false;
    }
    
    cJSON *quotes_array = cJSON_GetObjectItem(json, "quotes");
    if (!cJSON_IsArray(quotes_array)) {
        ESP_LOGE(TAG, "Quotes array not found in JSON");
        cJSON_Delete(json);
        return false;
    }
    
    int quote_count = cJSON_GetArraySize(quotes_array);
    if (quote_count == 0) {
        ESP_LOGE(TAG, "No quotes in array");
        cJSON_Delete(json);
        return false;
    }
    
    // Get random quote
    int random_index = esp_random() % quote_count;
    cJSON *quote_item = cJSON_GetArrayItem(quotes_array, random_index);
    
    if (!cJSON_IsObject(quote_item)) {
        ESP_LOGE(TAG, "Quote item is not an object");
        cJSON_Delete(json);
        return false;
    }
    
    // Extract quote text
    cJSON *quote_text = cJSON_GetObjectItem(quote_item, "quote");
    if (cJSON_IsString(quote_text)) {
        strncpy(quote_data->quote, quote_text->valuestring, sizeof(quote_data->quote) - 1);
        quote_data->quote[sizeof(quote_data->quote) - 1] = '\0';
    } else {
        cJSON_Delete(json);
        return false;
    }
    
    // Extract character
    cJSON *character = cJSON_GetObjectItem(quote_item, "character");
    if (cJSON_IsString(character)) {
        strncpy(quote_data->character, character->valuestring, sizeof(quote_data->character) - 1);
        quote_data->character[sizeof(quote_data->character) - 1] = '\0';
    } else {
        strcpy(quote_data->character, "Unknown");
    }
    
    // Extract episode
    cJSON *episode = cJSON_GetObjectItem(quote_item, "episode");
    if (cJSON_IsString(episode)) {
        strncpy(quote_data->episode, episode->valuestring, sizeof(quote_data->episode) - 1);
        quote_data->episode[sizeof(quote_data->episode) - 1] = '\0';
    } else {
        strcpy(quote_data->episode, "Unknown");
    }
    
    cJSON_Delete(json);
    
    ESP_LOGI(TAG, "Selected quote #%d: '%s' - %s (%s)", random_index, 
             quote_data->quote, quote_data->character, quote_data->episode);
    
    return true;
}

// Gesture event handler for quote screen (tap or slide to return to glucose)
static void quote_gesture_event(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    
    if (code == LV_EVENT_CLICKED || code == LV_EVENT_GESTURE) {
        ESP_LOGI(TAG, "Quote screen dismissed, returning to glucose screen");
        display_show_glucose(last_glucose_mmol, last_trend, last_is_low, last_is_high, last_timestamp, last_measurement_color);
    }
}

// Gesture event handler for glucose screen (slide down to show datetime/moon, slide up for quote)
static void glucose_gesture_event(lv_event_t *e) {
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
    
    if (dir == LV_DIR_BOTTOM) {
        ESP_LOGI(TAG, "Slide-down gesture detected, showing datetime/moon screen");
        display_show_datetime_moon();
    } else if (dir == LV_DIR_TOP) {
        ESP_LOGI(TAG, "Slide-up gesture detected, showing random quote");
        display_show_random_quote();
    }
}

// Gesture event handler for datetime/moon screen (slide up to return to glucose)
static void datetime_gesture_event(lv_event_t *e) {
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
    
    if (dir == LV_DIR_TOP) {
        ESP_LOGI(TAG, "Slide-up gesture detected, returning to glucose screen");
        // Restore last glucose screen
        display_show_glucose(last_glucose_mmol, last_trend, last_is_low, last_is_high, last_timestamp, last_measurement_color);
    }
}

// Tap event handler for surprise screen
static void glucose_screen_tap_event(lv_event_t *e)
{
    uint32_t current_time = lv_tick_get();
    
    // Reset tap count if more than 1 second since last tap
    if (current_time - last_tap_time > 1000) {
        tap_count = 0;
    }
    
    tap_count++;
    last_tap_time = current_time;
    
    ESP_LOGI(TAG, "Screen tapped %d times", tap_count);
    
    // Show surprise screen on triple tap
    if (tap_count >= 3) {
        tap_count = 0;
        display_show_surprise();
    }
}

void display_show_glucose(float glucose_mmol, const char *trend, bool is_low, bool is_high, const char *timestamp, int measurement_color)
{
    // Store values for restoring after surprise screen
    last_glucose_mmol = glucose_mmol;
    strncpy(last_trend, trend, sizeof(last_trend) - 1);
    last_trend[sizeof(last_trend) - 1] = '\0';
    last_is_low = is_low;
    last_is_high = is_high;
    strncpy(last_timestamp, timestamp ? timestamp : "Unknown", sizeof(last_timestamp) - 1);
    last_timestamp[sizeof(last_timestamp) - 1] = '\0';
    last_measurement_color = measurement_color;
    
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
    
    // Set background color based on measurement_color from LibreLink
    if (measurement_color == 3) {
        // Hypo (red)
        lv_obj_set_style_bg_color(screen, lv_color_make(255, 0, 0), 0);
        flash_timer = lv_timer_create(flash_timer_cb, 500, NULL); // Flash every 500ms for hypo
    } else if (measurement_color == 2) {
        // Warning/High (amber)
        lv_obj_set_style_bg_color(screen, lv_color_make(255, 165, 0), 0);
    } else {
        // Normal (green) - measurement_color == 1 or default
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
    lv_obj_align(status_label, LV_ALIGN_BOTTOM_MID, 0, -35);
    
    // Timestamp below status
    lv_obj_t *timestamp_label = lv_label_create(screen);
    char timestamp_text[64];
    snprintf(timestamp_text, sizeof(timestamp_text), "Last updated: %s", timestamp ? timestamp : "Unknown");
    lv_label_set_text(timestamp_label, timestamp_text);
    lv_obj_set_style_text_color(timestamp_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(timestamp_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(timestamp_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(timestamp_label, LV_ALIGN_BOTTOM_MID, 0, -15);
    
    // Add tap event for surprise screen
    lv_obj_add_event_cb(screen, glucose_screen_tap_event, LV_EVENT_CLICKED, NULL);
    
    // Add gesture event for slide-down to datetime/moon screen
    lv_obj_add_event_cb(screen, glucose_gesture_event, LV_EVENT_GESTURE, NULL);
    
    lv_screen_load(screen);
    current_screen = screen;
    
    display_unlock();
    
    // Send IR command to Moon Lamp if enabled
    ir_transmitter_set_moon_lamp_color(measurement_color);
    
    ESP_LOGI(TAG, "Glucose screen displayed: %.1f mmol/L %s (Low: %d, High: %d), Color: %d", 
             glucose_mmol, trend, is_low, is_high, measurement_color);
}

void display_show_no_recent_data(void)
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
    
    // Orange background for warning
    lv_obj_set_style_bg_color(screen, lv_color_make(255, 165, 0), 0);
    
    // Large "No recent data" message
    lv_obj_t *message_label = lv_label_create(screen);
    lv_label_set_text(message_label, "No recent\ndata");
    lv_obj_set_style_text_color(message_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(message_label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_align(message_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(message_label, LV_ALIGN_CENTER, 0, 0);
    
    lv_screen_load(screen);
    current_screen = screen;
    
    display_unlock();
    
    ESP_LOGI(TAG, "No recent data screen displayed");
}

void display_show_datetime_moon(void)
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
    
    // Dark blue/purple background for night sky theme
    lv_obj_set_style_bg_color(screen, lv_color_make(20, 20, 50), 0);
    
    // Get current time
    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    
    // Check if time is valid (year > 2020)
    bool time_valid = (timeinfo.tm_year + 1900) > 2020;
    
    if (!time_valid) {
        // Show "Time not synced" message
        lv_obj_t *error_label = lv_label_create(screen);
        lv_label_set_text(error_label, "Time not\nsynced yet");
        lv_obj_set_style_text_color(error_label, lv_color_white(), 0);
        lv_obj_set_style_text_font(error_label, &lv_font_montserrat_48, 0);
        lv_obj_set_style_text_align(error_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(error_label);
    } else {
        // Calculate moon phase
        float moonAge = calculate_moon_age();
        const char* moonPhase = get_moon_phase_name(moonAge);
        
        // Calculate days until next full moon
        // Full moon occurs at ~14.765 days (half of 29.53 day cycle)
        float daysToFullMoon;
        if (moonAge < 14.765f) {
            daysToFullMoon = 14.765f - moonAge;
        } else {
            daysToFullMoon = 29.53f - moonAge + 14.765f;
        }
        
        // Calculate the date of next full moon
        time_t next_full_moon_time = now + (time_t)(daysToFullMoon * 86400); // 86400 seconds per day
        struct tm next_full_moon_tm;
        localtime_r(&next_full_moon_time, &next_full_moon_tm);
        
        // Date at top (Day, DD Month YYYY)
        lv_obj_t *date_label = lv_label_create(screen);
        char date_text[64];
        const char* weekdays[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
        const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
        snprintf(date_text, sizeof(date_text), "%s, %d %s %d", 
                 weekdays[timeinfo.tm_wday], 
                 timeinfo.tm_mday, 
                 months[timeinfo.tm_mon],
                 timeinfo.tm_year + 1900);
        lv_label_set_text(date_label, date_text);
        lv_obj_set_style_text_color(date_label, lv_color_white(), 0);
        lv_obj_set_style_text_font(date_label, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_align(date_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(date_label, LV_ALIGN_TOP_MID, 0, 20);
        
        // Large time display (HH:MM)
        lv_obj_t *time_label = lv_label_create(screen);
        char time_text[16];
        snprintf(time_text, sizeof(time_text), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
        lv_label_set_text(time_label, time_text);
        lv_obj_set_style_text_color(time_label, lv_color_white(), 0);
        lv_obj_set_style_text_font(time_label, &lv_font_montserrat_48, 0);
        lv_obj_set_style_text_align(time_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_transform_scale(time_label, 250, 0);  // Scale up 2.5x
        lv_obj_align(time_label, LV_ALIGN_CENTER, 0, -20);
        
        // Moon phase name
        lv_obj_t *moon_phase_label = lv_label_create(screen);
        lv_label_set_text(moon_phase_label, moonPhase);
        lv_obj_set_style_text_color(moon_phase_label, lv_color_make(200, 200, 255), 0); // Light blue
        lv_obj_set_style_text_font(moon_phase_label, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_align(moon_phase_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(moon_phase_label, LV_ALIGN_BOTTOM_MID, 0, -60);
        
        // Next full moon info
        lv_obj_t *full_moon_label = lv_label_create(screen);
        char full_moon_text[80];
        snprintf(full_moon_text, sizeof(full_moon_text), "Next Full Moon: %s %d %s", 
                 weekdays[next_full_moon_tm.tm_wday],
                 next_full_moon_tm.tm_mday,
                 months[next_full_moon_tm.tm_mon]);
        lv_label_set_text(full_moon_label, full_moon_text);
        lv_obj_set_style_text_color(full_moon_label, lv_color_make(255, 255, 200), 0); // Slight yellow tint
        lv_obj_set_style_text_font(full_moon_label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_align(full_moon_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(full_moon_label, LV_ALIGN_BOTTOM_MID, 0, -40);
        
        // Instruction text at bottom
        lv_obj_t *instruction_label = lv_label_create(screen);
        lv_label_set_text(instruction_label, "Slide up to return");
        lv_obj_set_style_text_color(instruction_label, lv_color_make(150, 150, 150), 0); // Gray
        lv_obj_set_style_text_font(instruction_label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_align(instruction_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(instruction_label, LV_ALIGN_BOTTOM_MID, 0, -5);
        
        ESP_LOGI(TAG, "DateTime/Moon screen displayed: %s, Moon: %s, Next Full Moon: %s %d %s", 
                 date_text, moonPhase, 
                 weekdays[next_full_moon_tm.tm_wday],
                 next_full_moon_tm.tm_mday,
                 months[next_full_moon_tm.tm_mon]);
    }
    
    // Add gesture event for slide-up to return to glucose screen
    lv_obj_add_event_cb(screen, datetime_gesture_event, LV_EVENT_GESTURE, NULL);
    
    lv_screen_load(screen);
    current_screen = screen;
    
    display_unlock();
}

void display_show_random_quote(void)
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
    
    // Deep purple background for mystical theme
    lv_obj_set_style_bg_color(screen, lv_color_make(50, 20, 60), 0);
    
    // Get random quote
    quote_data_t quote_data;
    bool success = get_random_quote(&quote_data);
    
    if (!success) {
        // Fallback if JSON parsing fails
        strcpy(quote_data.quote, "The power within you is stronger than you know.");
        strcpy(quote_data.character, "Unknown");
        strcpy(quote_data.episode, "Unknown");
    }
    
    // Quote text
    lv_obj_t *quote_label = lv_label_create(screen);
    lv_label_set_text(quote_label, quote_data.quote);
    lv_obj_set_style_text_color(quote_label, lv_color_make(220, 200, 255), 0); // Light purple
    lv_obj_set_style_text_font(quote_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_align(quote_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(quote_label, 280); // Allow text wrapping
    lv_label_set_long_mode(quote_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(quote_label, LV_ALIGN_CENTER, 0, -20);
    
    // Character and episode attribution
    lv_obj_t *attribution_label = lv_label_create(screen);
    char attribution_text[150];
    snprintf(attribution_text, sizeof(attribution_text), "%s (%s)", 
             quote_data.character, quote_data.episode);
    lv_label_set_text(attribution_label, attribution_text);
    lv_obj_set_style_text_color(attribution_label, lv_color_make(180, 160, 200), 0); // Slightly darker purple
    lv_obj_set_style_text_font(attribution_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(attribution_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(attribution_label, 280);
    lv_label_set_long_mode(attribution_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(attribution_label, LV_ALIGN_CENTER, 0, 60);
    
    // Instruction text at bottom
    lv_obj_t *instruction_label = lv_label_create(screen);
    lv_label_set_text(instruction_label, "Tap or swipe to return");
    lv_obj_set_style_text_color(instruction_label, lv_color_make(150, 150, 150), 0); // Gray
    lv_obj_set_style_text_font(instruction_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(instruction_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(instruction_label, LV_ALIGN_BOTTOM_MID, 0, -10);
    
    // Add gesture and tap event to return to glucose screen
    lv_obj_add_event_cb(screen, quote_gesture_event, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(screen, quote_gesture_event, LV_EVENT_GESTURE, NULL);
    
    lv_screen_load(screen);
    current_screen = screen;
    
    display_unlock();
    
    ESP_LOGI(TAG, "Random quote screen displayed");
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
static display_button_callback_t about_callback = NULL;

static void reset_button_event(lv_event_t *e) {
    if (reset_callback) {
        reset_callback();
    }
}

static void about_button_event(lv_event_t *e) {
    if (about_callback) {
        about_callback();
    }
}

void display_show_settings(display_button_callback_t reset_cb, display_button_callback_t about_cb)
{
    reset_callback = reset_cb;
    about_callback = about_cb;
    
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
    lv_obj_align(config_url, LV_ALIGN_CENTER, 0, -60);
    
    // About button
    lv_obj_t *about_btn = lv_btn_create(screen);
    lv_obj_set_size(about_btn, 200, 50);
    lv_obj_align(about_btn, LV_ALIGN_CENTER, 0, 10);
    lv_obj_set_style_bg_color(about_btn, lv_color_make(100, 100, 255), 0);
    lv_obj_add_event_cb(about_btn, about_button_event, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *about_label = lv_label_create(about_btn);
    lv_label_set_text(about_label, "About");
    lv_obj_set_style_text_font(about_label, &lv_font_montserrat_18, 0);
    lv_obj_center(about_label);
    
    // Reset button
    lv_obj_t *reset_btn = lv_btn_create(screen);
    lv_obj_set_size(reset_btn, 200, 50);
    lv_obj_align(reset_btn, LV_ALIGN_CENTER, 0, 70);
    lv_obj_set_style_bg_color(reset_btn, lv_color_make(255, 100, 100), 0);
    lv_obj_add_event_cb(reset_btn, reset_button_event, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *reset_label = lv_label_create(reset_btn);
    lv_label_set_text(reset_label, "Reset Device");
    lv_obj_set_style_text_font(reset_label, &lv_font_montserrat_18, 0);
    lv_obj_center(reset_label);
    
    lv_screen_load(screen);
    current_screen = screen;
    
    display_unlock();
    
    ESP_LOGI(TAG, "Settings screen displayed");
}

// Static callback for about screen
static display_button_callback_t about_back_callback = NULL;

static void about_back_button_event(lv_event_t *e) {
    if (about_back_callback) {
        about_back_callback();
    }
}

void display_show_about_message(display_button_callback_t back_cb)
{
    about_back_callback = back_cb;
    
    display_lock();
    
    if (current_screen) {
        lv_obj_del(current_screen);
    }
    
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    
    // Title
    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "About");
    lv_obj_set_style_text_color(title, lv_color_make(76, 175, 80), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);
    
    // Message
    lv_obj_t *message = lv_label_create(screen);
    char about_text[200];
    snprintf(about_text, sizeof(about_text), 
             "For the Supreme (Stephen Higgins),\ndeveloped with love by\nSpalding (Derek Marr).\n\nOderint dum metuant.\n\n%s", 
             DEVICE_VERSION);
    lv_label_set_text(message, about_text);
    lv_obj_set_style_text_color(message, lv_color_make(200, 200, 200), 0);
    lv_obj_set_style_text_font(message, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(message, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(message, LV_ALIGN_CENTER, 0, 0);
    
    // Back button
    lv_obj_t *back_btn = lv_btn_create(screen);
    lv_obj_set_size(back_btn, 200, 50);
    lv_obj_align(back_btn, LV_ALIGN_BOTTOM_MID, 0, -30);
    lv_obj_set_style_bg_color(back_btn, lv_color_make(100, 100, 255), 0);
    lv_obj_add_event_cb(back_btn, about_back_button_event, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, "Back");
    lv_obj_set_style_text_font(back_label, &lv_font_montserrat_18, 0);
    lv_obj_center(back_label);
    
    lv_screen_load(screen);
    current_screen = screen;
    
    display_unlock();
    
    ESP_LOGI(TAG, "About message screen displayed");
}

void display_show_librelink_qr(const char *ip)
{
    display_lock();
    
    if (current_screen) {
        lv_obj_del(current_screen);
    }
    
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    
    // Title
    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "Setup LibreLink");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);
    
    // Generate URL
    char url[128];
    snprintf(url, sizeof(url), "http://%s/librelink", ip);
    
    // Create QR code
    lv_obj_t *qr = lv_qrcode_create(screen);
    lv_qrcode_set_size(qr, 160);
    lv_qrcode_set_dark_color(qr, lv_color_black());
    lv_qrcode_set_light_color(qr, lv_color_white());
    
    // Generate QR code data
    lv_qrcode_update(qr, url, strlen(url));
    lv_obj_center(qr);
    
    // Instruction text
    lv_obj_t *instruction = lv_label_create(screen);
    lv_label_set_text(instruction, "Scan QR code to configure\nyour LibreLink credentials");
    lv_obj_set_style_text_color(instruction, lv_color_white(), 0);
    lv_obj_set_style_text_font(instruction, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(instruction, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(instruction, LV_ALIGN_BOTTOM_MID, 0, -10);
    
    lv_screen_load(screen);
    current_screen = screen;
    
    display_unlock();
    
    ESP_LOGI(TAG, "LibreLink QR code screen displayed: %s", url);
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

// Static storage for OTA warning screen elements
static lv_obj_t *ota_warning_proceed_btn = NULL;
static lv_obj_t *ota_warning_cancel_btn = NULL;
static lv_obj_t *ota_warning_text = NULL;

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
    
    ota_warning_text = lv_label_create(warning_box);
    lv_label_set_text(ota_warning_text, 
        "WARNING!\n\n"
        "Do NOT disconnect power\n"
        "during the update process.\n"
        "Device will reboot when\n"
        "update is complete.");
    lv_obj_set_style_text_color(ota_warning_text, lv_color_white(), 0);
    lv_obj_set_style_text_font(ota_warning_text, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(ota_warning_text, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(ota_warning_text, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(ota_warning_text, 260);
    lv_obj_center(ota_warning_text);
    
    // Proceed button
    ota_warning_proceed_btn = lv_btn_create(screen);
    lv_obj_set_size(ota_warning_proceed_btn, 130, 45);
    lv_obj_align(ota_warning_proceed_btn, LV_ALIGN_BOTTOM_LEFT, 20, -20);
    lv_obj_set_style_bg_color(ota_warning_proceed_btn, lv_color_make(0, 150, 0), 0);
    lv_obj_add_event_cb(ota_warning_proceed_btn, ota_warning_proceed_event, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *proceed_label = lv_label_create(ota_warning_proceed_btn);
    lv_label_set_text(proceed_label, "Update Now");
    lv_obj_set_style_text_font(proceed_label, &lv_font_montserrat_14, 0);
    lv_obj_center(proceed_label);
    
    // Cancel button
    ota_warning_cancel_btn = lv_btn_create(screen);
    lv_obj_set_size(ota_warning_cancel_btn, 130, 45);
    lv_obj_align(ota_warning_cancel_btn, LV_ALIGN_BOTTOM_RIGHT, -20, -20);
    lv_obj_set_style_bg_color(ota_warning_cancel_btn, lv_color_make(100, 100, 100), 0);
    lv_obj_add_event_cb(ota_warning_cancel_btn, ota_warning_cancel_event, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *cancel_label = lv_label_create(ota_warning_cancel_btn);
    lv_label_set_text(cancel_label, "Later");
    lv_obj_set_style_text_font(cancel_label, &lv_font_montserrat_14, 0);
    lv_obj_center(cancel_label);
    
    lv_screen_load(screen);
    current_screen = screen;
    
    display_unlock();
    
    ESP_LOGI(TAG, "OTA warning screen displayed");
}

void display_ota_warning_start_update(void)
{
    display_lock();
    
    // Delete the buttons
    if (ota_warning_proceed_btn) {
        lv_obj_del(ota_warning_proceed_btn);
        ota_warning_proceed_btn = NULL;
    }
    if (ota_warning_cancel_btn) {
        lv_obj_del(ota_warning_cancel_btn);
        ota_warning_cancel_btn = NULL;
    }
    
    // Update warning text to "Updating..."
    if (ota_warning_text) {
        lv_label_set_text(ota_warning_text, "Updating...\n\nPlease wait");
    }
    
    // Force LVGL to invalidate and refresh the display immediately
    if (current_screen) {
        lv_obj_invalidate(current_screen);
    }
    
    display_unlock();
    
    // Force LVGL task to process the updates NOW before returning
    lv_timer_handler();
    
    ESP_LOGI(TAG, "OTA warning transitioned to updating state");
}
