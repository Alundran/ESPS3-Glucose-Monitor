/**
 * LibreLink Credentials Storage Implementation
 */

#include "libre_credentials.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>

static const char *TAG = "LIBRE_CRED";
static const char *LIBRE_NAMESPACE = "libre";
static const char *LIBRE_EMAIL_KEY = "email";
static const char *LIBRE_PASS_KEY = "password";
static const char *LIBRE_PATIENT_KEY = "patient_id";
static const char *LIBRE_SERVER_KEY = "use_eu";

esp_err_t libre_credentials_save(const char *email, const char *password, 
                                  const char *patient_id, bool use_eu_server)
{
    if (!email || !password) {
        return ESP_ERR_INVALID_ARG;
    }
    
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(LIBRE_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS: %s", esp_err_to_name(err));
        return err;
    }
    
    // Save credentials
    nvs_set_str(nvs_handle, LIBRE_EMAIL_KEY, email);
    nvs_set_str(nvs_handle, LIBRE_PASS_KEY, password);
    
    if (patient_id) {
        nvs_set_str(nvs_handle, LIBRE_PATIENT_KEY, patient_id);
    }
    
    nvs_set_u8(nvs_handle, LIBRE_SERVER_KEY, use_eu_server ? 1 : 0);
    
    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "LibreLink credentials saved");
    } else {
        ESP_LOGE(TAG, "Error saving credentials: %s", esp_err_to_name(err));
    }
    
    return err;
}

esp_err_t libre_credentials_load(char *email, char *password, 
                                  char *patient_id, bool *use_eu_server)
{
    if (!email || !password) {
        return ESP_ERR_INVALID_ARG;
    }
    
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(LIBRE_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }
    
    size_t len = 128;
    err = nvs_get_str(nvs_handle, LIBRE_EMAIL_KEY, email, &len);
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        return err;
    }
    
    len = 128;
    err = nvs_get_str(nvs_handle, LIBRE_PASS_KEY, password, &len);
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        return err;
    }
    
    // Optional fields
    if (patient_id) {
        len = 64;
        if (nvs_get_str(nvs_handle, LIBRE_PATIENT_KEY, patient_id, &len) != ESP_OK) {
            patient_id[0] = '\0';
        }
    }
    
    if (use_eu_server) {
        uint8_t val = 0;
        if (nvs_get_u8(nvs_handle, LIBRE_SERVER_KEY, &val) == ESP_OK) {
            *use_eu_server = (val == 1);
        } else {
            *use_eu_server = false;
        }
    }
    
    nvs_close(nvs_handle);
    return ESP_OK;
}

bool libre_credentials_exist(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(LIBRE_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        return false;
    }
    
    size_t len = 0;
    err = nvs_get_str(nvs_handle, LIBRE_EMAIL_KEY, NULL, &len);
    nvs_close(nvs_handle);
    
    return (err == ESP_OK && len > 0);
}

esp_err_t libre_credentials_clear(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(LIBRE_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }
    
    nvs_erase_key(nvs_handle, LIBRE_EMAIL_KEY);
    nvs_erase_key(nvs_handle, LIBRE_PASS_KEY);
    nvs_erase_key(nvs_handle, LIBRE_PATIENT_KEY);
    nvs_erase_key(nvs_handle, LIBRE_SERVER_KEY);
    
    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    
    ESP_LOGI(TAG, "LibreLink credentials cleared");
    return err;
}
