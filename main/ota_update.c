/**
 * OTA Update Module Implementation
 */

#include "ota_update.h"
#include "config.h"
#include "wifi_manager.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "OTA_UPDATE";

// Buffer for GitHub API response
#define MAX_HTTP_RECV_BUFFER 4096
static char http_recv_buffer[MAX_HTTP_RECV_BUFFER];
static int http_recv_len = 0;

// OTA progress tracking
static ota_progress_callback_t global_progress_cb = NULL;

/**
 * HTTP event handler for GitHub API requests
 */
static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (evt->data_len > 0 && (http_recv_len + evt->data_len) < MAX_HTTP_RECV_BUFFER) {
                memcpy(http_recv_buffer + http_recv_len, evt->data, evt->data_len);
                http_recv_len += evt->data_len;
                http_recv_buffer[http_recv_len] = '\0';
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}

/**
 * Compare semantic versions (e.g., "1.0.1" vs "1.0.0")
 * @return >0 if v1 > v2, 0 if equal, <0 if v1 < v2
 */
static int compare_versions(const char *v1, const char *v2) {
    int major1 = 0, minor1 = 0, patch1 = 0;
    int major2 = 0, minor2 = 0, patch2 = 0;
    
    sscanf(v1, "%d.%d.%d", &major1, &minor1, &patch1);
    sscanf(v2, "%d.%d.%d", &major2, &minor2, &patch2);
    
    if (major1 != major2) return major1 - major2;
    if (minor1 != minor2) return minor1 - minor2;
    return patch1 - patch2;
}

esp_err_t ota_update_init(void) {
    ESP_LOGI(TAG, "OTA Update system initialized");
    ESP_LOGI(TAG, "Current firmware version: %s", DEVICE_VERSION);
    
    // Print partition information
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *update = esp_ota_get_next_update_partition(NULL);
    
    ESP_LOGI(TAG, "Running partition: %s at 0x%lx (size: %lu bytes)", 
             running->label, running->address, running->size);
    if (update) {
        ESP_LOGI(TAG, "Update partition: %s at 0x%lx (size: %lu bytes)", 
                 update->label, update->address, update->size);
    }
    
    return ESP_OK;
}

esp_err_t ota_check_for_update(char *new_version, size_t new_version_size) {
    if (!wifi_manager_is_connected()) {
        ESP_LOGW(TAG, "Cannot check for updates - WiFi not connected");
        return ESP_ERR_WIFI_NOT_CONNECT;
    }
    
    ESP_LOGI(TAG, "Checking for firmware updates from GitHub...");
    ESP_LOGI(TAG, "API URL: %s", GITHUB_API_URL);
    
    // Reset buffer
    http_recv_len = 0;
    memset(http_recv_buffer, 0, sizeof(http_recv_buffer));
    
    // Configure HTTP client for GitHub API
    esp_http_client_config_t config = {
        .url = GITHUB_API_URL,
        .event_handler = http_event_handler,
        .timeout_ms = 10000,
        .user_agent = "ESP32-Glucose-Monitor/1.0",
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        return ESP_FAIL;
    }
    
    // Perform GET request
    esp_err_t err = esp_http_client_perform(client);
    int status_code = esp_http_client_get_status_code(client);
    
    esp_http_client_cleanup(client);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
        return err;
    }
    
    if (status_code != 200) {
        ESP_LOGE(TAG, "GitHub API returned status code: %d", status_code);
        return ESP_FAIL;
    }
    
    // Parse JSON response
    cJSON *json = cJSON_Parse(http_recv_buffer);
    if (!json) {
        ESP_LOGE(TAG, "Failed to parse GitHub API response");
        return ESP_FAIL;
    }
    
    // Get tag name (version)
    cJSON *tag_name = cJSON_GetObjectItem(json, "tag_name");
    if (!tag_name || !cJSON_IsString(tag_name)) {
        ESP_LOGE(TAG, "No tag_name found in GitHub response");
        cJSON_Delete(json);
        return ESP_FAIL;
    }
    
    // Remove 'v' prefix if present (e.g., "v1.0.1" -> "1.0.1")
    const char *version_str = tag_name->valuestring;
    if (version_str[0] == 'v' || version_str[0] == 'V') {
        version_str++;
    }
    
    ESP_LOGI(TAG, "Latest GitHub release: %s", version_str);
    ESP_LOGI(TAG, "Current version: %s", DEVICE_VERSION);
    
    // Compare versions
    if (compare_versions(version_str, DEVICE_VERSION) > 0) {
        ESP_LOGI(TAG, "Update available! %s -> %s", DEVICE_VERSION, version_str);
        if (new_version && new_version_size > 0) {
            strncpy(new_version, version_str, new_version_size - 1);
            new_version[new_version_size - 1] = '\0';
        }
        cJSON_Delete(json);
        return ESP_OK;
    } else {
        ESP_LOGI(TAG, "Already running latest version");
        cJSON_Delete(json);
        return ESP_ERR_NOT_FOUND;
    }
}

/**
 * OTA progress handler - called during download/install
 */
static void ota_progress_handler(size_t total_size, size_t current_size) {
    if (total_size > 0 && global_progress_cb) {
        int progress = (current_size * 100) / total_size;
        
        if (progress <= 50) {
            global_progress_cb(progress, "Downloading firmware...");
        } else {
            global_progress_cb(progress, "Installing firmware...");
        }
    }
}

esp_err_t ota_perform_update(ota_progress_callback_t progress_cb) {
    if (!ota_is_safe_to_update()) {
        ESP_LOGE(TAG, "Not safe to update - WiFi or power issue");
        return ESP_FAIL;
    }
    
    global_progress_cb = progress_cb;
    
    ESP_LOGI(TAG, "Starting OTA update from GitHub...");
    
    if (progress_cb) {
        progress_cb(0, "Checking for updates...");
    }
    
    // Get latest release information
    http_recv_len = 0;
    memset(http_recv_buffer, 0, sizeof(http_recv_buffer));
    
    esp_http_client_config_t api_config = {
        .url = GITHUB_API_URL,
        .event_handler = http_event_handler,
        .timeout_ms = 10000,
        .user_agent = "ESP32-Glucose-Monitor/1.0",
    };
    
    esp_http_client_handle_t api_client = esp_http_client_init(&api_config);
    if (!api_client) {
        ESP_LOGE(TAG, "Failed to initialize API client");
        return ESP_FAIL;
    }
    
    esp_err_t err = esp_http_client_perform(api_client);
    int status_code = esp_http_client_get_status_code(api_client);
    esp_http_client_cleanup(api_client);
    
    if (err != ESP_OK || status_code != 200) {
        ESP_LOGE(TAG, "Failed to fetch release info");
        return ESP_FAIL;
    }
    
    // Parse JSON to get download URL
    cJSON *json = cJSON_Parse(http_recv_buffer);
    if (!json) {
        ESP_LOGE(TAG, "Failed to parse release info");
        return ESP_FAIL;
    }
    
    // Find .bin file in assets
    cJSON *assets = cJSON_GetObjectItem(json, "assets");
    if (!assets || !cJSON_IsArray(assets)) {
        ESP_LOGE(TAG, "No assets found in release");
        cJSON_Delete(json);
        return ESP_FAIL;
    }
    
    char *download_url = NULL;
    cJSON *asset = NULL;
    cJSON_ArrayForEach(asset, assets) {
        cJSON *name = cJSON_GetObjectItem(asset, "name");
        if (name && cJSON_IsString(name)) {
            const char *filename = name->valuestring;
            // Look for .bin file
            if (strstr(filename, ".bin") != NULL) {
                cJSON *browser_download_url = cJSON_GetObjectItem(asset, "browser_download_url");
                if (browser_download_url && cJSON_IsString(browser_download_url)) {
                    download_url = strdup(browser_download_url->valuestring);
                    ESP_LOGI(TAG, "Found firmware: %s", filename);
                    break;
                }
            }
        }
    }
    
    cJSON_Delete(json);
    
    if (!download_url) {
        ESP_LOGE(TAG, "No .bin file found in release assets");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Downloading firmware from: %s", download_url);
    
    if (progress_cb) {
        progress_cb(5, "Starting download...");
    }
    
    // Configure OTA
    esp_http_client_config_t ota_config = {
        .url = download_url,
        .timeout_ms = 30000,
        .keep_alive_enable = true,
    };
    
    esp_https_ota_config_t ota_https_config = {
        .http_config = &ota_config,
    };
    
    esp_https_ota_handle_t ota_handle = NULL;
    err = esp_https_ota_begin(&ota_https_config, &ota_handle);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA begin failed: %s", esp_err_to_name(err));
        free(download_url);
        return err;
    }
    
    // Download and install with progress
    size_t total_size = esp_https_ota_get_image_size(ota_handle);
    ESP_LOGI(TAG, "Firmware size: %d bytes", total_size);
    
    while (1) {
        err = esp_https_ota_perform(ota_handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            break;
        }
        
        // Update progress
        size_t current_size = esp_https_ota_get_image_len_read(ota_handle);
        ota_progress_handler(total_size, current_size);
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "OTA download complete, verifying...");
        if (progress_cb) {
            progress_cb(95, "Verifying firmware...");
        }
    } else {
        ESP_LOGE(TAG, "OTA download failed: %s", esp_err_to_name(err));
        esp_https_ota_abort(ota_handle);
        free(download_url);
        return err;
    }
    
    // Finish OTA update
    err = esp_https_ota_finish(ota_handle);
    free(download_url);
    
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "OTA update successful! Rebooting...");
        if (progress_cb) {
            progress_cb(100, "Update complete! Rebooting...");
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_restart();
    } else {
        ESP_LOGE(TAG, "OTA finish failed: %s", esp_err_to_name(err));
        return err;
    }
    
    return ESP_OK;
}

bool ota_is_safe_to_update(void) {
    // Check WiFi
    if (!wifi_manager_is_connected()) {
        ESP_LOGW(TAG, "Not safe to update - WiFi not connected");
        return false;
    }
    
    // For ESP32-S3-BOX-3, it's always powered via USB, so we assume power is good
    // In a battery-powered device, you'd check battery level here
    
    return true;
}

const char* ota_get_current_version(void) {
    return DEVICE_VERSION;
}
