/**
 * LibreLinkUp API Client Implementation
 */

#include "librelinkup.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_tls.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include "mbedtls/sha256.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>

static const char *TAG = "LIBRELINKUP";

// Configuration
static char api_url[64] = LIBRELINKUP_API_URL_GLOBAL;
static char auth_token[512] = {0};
static char account_id[65] = {0};  // SHA256 hash in hex (64 chars + null terminator)
static bool logged_in = false;
static bool api_url_set_by_redirect = false;  // Track if URL was set by regional redirect

// HTTP response buffer (increased to handle large glucose graph responses)
#define HTTP_BUFFER_SIZE 16384
static char http_response[HTTP_BUFFER_SIZE];
static int http_response_len = 0;

// Store graph data from last fetch
static libre_graph_data_t cached_graph_data = {0};

/**
 * HTTP event handler
 */
static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            // Ensure we have room for data + null terminator
            if (http_response_len + evt->data_len < HTTP_BUFFER_SIZE - 1) {
                memcpy(http_response + http_response_len, evt->data, evt->data_len);
                http_response_len += evt->data_len;
                http_response[http_response_len] = '\0';
            } else {
                ESP_LOGW(TAG, "Response buffer overflow");
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}

/**
 * Retry HTTP requests with exponential backoff for DNS/network failures
 * This helps recover from transient DNS issues after OTA reboots
 */
static esp_err_t http_client_perform_with_retry(esp_http_client_handle_t client, int max_retries)
{
    esp_err_t err = ESP_FAIL;
    int retry_delay_ms = 1000;  // Start with 1 second
    
    for (int retry = 0; retry < max_retries; retry++) {
        err = esp_http_client_perform(client);
        
        if (err == ESP_OK) {
            return ESP_OK;
        }
        
        // Only retry on connection/DNS failures, not on HTTP errors
        if (err == ESP_ERR_HTTP_CONNECT || 
            err == ESP_FAIL ||  // DNS lookup failures return ESP_FAIL
            err == ESP_ERR_TIMEOUT) {
            
            if (retry < max_retries - 1) {
                ESP_LOGW(TAG, "HTTP request failed (%s), retrying in %d ms (%d/%d)", 
                         esp_err_to_name(err), retry_delay_ms, retry + 1, max_retries);
                vTaskDelay(pdMS_TO_TICKS(retry_delay_ms));
                retry_delay_ms *= 2;  // Exponential backoff
                
                if (retry_delay_ms > 5000) {
                    retry_delay_ms = 5000;  // Cap at 5 seconds
                }
            } else {
                ESP_LOGE(TAG, "HTTP request failed after %d retries: %s", max_retries, esp_err_to_name(err));
            }
        } else {
            // Don't retry on other types of errors
            return err;
        }
    }
    
    return err;
}

esp_err_t librelinkup_init(bool use_eu_server)
{
    // Try to load saved auth token and account_id from NVS first
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("storage", NVS_READONLY, &nvs_handle);
    if (err == ESP_OK) {
        // Try to load auth token
        size_t token_size = sizeof(auth_token);
        err = nvs_get_str(nvs_handle, "auth_token", auth_token, &token_size);
        if (err == ESP_OK && strlen(auth_token) > 0) {
            // Try to load account_id
            size_t account_size = sizeof(account_id);
            err = nvs_get_str(nvs_handle, "account_id", account_id, &account_size);
            if (err == ESP_OK && strlen(account_id) > 0) {
                logged_in = true;
                ESP_LOGI(TAG, "Restored auth token from NVS (valid for ~6 months)");
                ESP_LOGI(TAG, "Token length: %d, Account-Id length: %d", strlen(auth_token), strlen(account_id));
            }
        }
        nvs_close(nvs_handle);
    }
    
    // Try to load saved regional URL from NVS
    err = nvs_open("storage", NVS_READONLY, &nvs_handle);
    if (err == ESP_OK) {
        size_t required_size = sizeof(api_url);
        err = nvs_get_str(nvs_handle, "api_url", api_url, &required_size);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Loaded regional API URL from NVS: %s", api_url);
            api_url_set_by_redirect = true;
            nvs_close(nvs_handle);
            return ESP_OK;
        }
        nvs_close(nvs_handle);
    }
    
    // No saved regional URL, use base URL if we haven't received a redirect
    if (!api_url_set_by_redirect) {
        if (use_eu_server) {
            strncpy(api_url, LIBRELINKUP_API_URL_EU, sizeof(api_url) - 1);
        } else {
            strncpy(api_url, LIBRELINKUP_API_URL_GLOBAL, sizeof(api_url) - 1);
        }
        ESP_LOGI(TAG, "Initialized with API URL: %s", api_url);
    } else {
        ESP_LOGI(TAG, "Using regional API URL from redirect: %s", api_url);
    }
    
    return ESP_OK;
}

esp_err_t librelinkup_login(const char *email, const char *password)
{
#if DEMO_MODE_ENABLED
    ESP_LOGI(TAG, "[DEMO MODE] Skipping API login - using dummy data");
    strcpy(auth_token, "demo_auth_token_12345");
    logged_in = true;
    return ESP_OK;
#else
    esp_err_t ret = ESP_FAIL;
    
    ESP_LOGI(TAG, "Logging in to LibreLinkUp...");
    
    // Create JSON request body
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "email", email);
    cJSON_AddStringToObject(root, "password", password);
    char *post_data = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    if (!post_data) {
        ESP_LOGE(TAG, "Failed to create JSON request");
        return ESP_ERR_NO_MEM;
    }
    
    // Reset response buffer
    http_response_len = 0;
    memset(http_response, 0, sizeof(http_response));
    
    // Configure HTTP client
    char url[128];
    snprintf(url, sizeof(url), "%s/llu/auth/login", api_url);
    
    ESP_LOGI(TAG, "Calling API: %s", url);
    
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .event_handler = http_event_handler,
        .timeout_ms = 10000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size_tx = 2048,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    
    // Set headers
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "product", "llu.android");
    esp_http_client_set_header(client, "version", "4.16.0");
    esp_http_client_set_header(client, "Cache-Control", "no-cache");
    esp_http_client_set_header(client, "Connection", "Keep-Alive");
    
    esp_http_client_set_post_field(client, post_data, strlen(post_data));
    
    // Perform request with retry logic for DNS failures
    esp_err_t err = http_client_perform_with_retry(client, 3);
    
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "HTTP Status: %d, Response length: %d", status_code, http_response_len);
        ESP_LOGI(TAG, "Response: %s", http_response);
        
        if (status_code == 200) {
            // Parse JSON response
            cJSON *json = cJSON_Parse(http_response);
            if (json) {
                cJSON *status = cJSON_GetObjectItem(json, "status");
                if (status && status->valueint == 0) {
                    // Extract auth token or handle redirect
                    cJSON *data = cJSON_GetObjectItem(json, "data");
                    if (data) {
                        // Check for redirect
                        cJSON *redirect = cJSON_GetObjectItem(data, "redirect");
                        if (redirect && cJSON_IsTrue(redirect)) {
                            cJSON *region = cJSON_GetObjectItem(data, "region");
                            if (region && region->valuestring) {
                                ESP_LOGI(TAG, "Redirecting to region: %s", region->valuestring);
                                // Update API URL with region and mark it as set by redirect
                                snprintf(api_url, sizeof(api_url), "https://api-%s.libreview.io", region->valuestring);
                                api_url_set_by_redirect = true;
                                
                                // Save regional URL to NVS for persistence across reboots
                                nvs_handle_t nvs_handle;
                                esp_err_t nvs_err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
                                if (nvs_err == ESP_OK) {
                                    nvs_err = nvs_set_str(nvs_handle, "api_url", api_url);
                                    if (nvs_err == ESP_OK) {
                                        nvs_commit(nvs_handle);
                                        ESP_LOGI(TAG, "Saved regional URL to NVS: %s", api_url);
                                    } else {
                                        ESP_LOGW(TAG, "Failed to save regional URL to NVS: %s", esp_err_to_name(nvs_err));
                                    }
                                    nvs_close(nvs_handle);
                                } else {
                                    ESP_LOGW(TAG, "Failed to open NVS for saving regional URL: %s", esp_err_to_name(nvs_err));
                                }
                                
                                cJSON_Delete(json);
                                esp_http_client_cleanup(client);
                                free(post_data);
                                // Retry login with new URL
                                return librelinkup_login(email, password);
                            }
                        }
                        
                        // Extract auth token
                        cJSON *auth_ticket = cJSON_GetObjectItem(data, "authTicket");
                        if (auth_ticket) {
                            cJSON *token = cJSON_GetObjectItem(auth_ticket, "token");
                            if (token && token->valuestring) {
                                strncpy(auth_token, token->valuestring, sizeof(auth_token) - 1);
                                
                                // Extract user ID and compute Account-Id (SHA256 hash)
                                cJSON *user = cJSON_GetObjectItem(data, "user");
                                if (user) {
                                    cJSON *user_id = cJSON_GetObjectItem(user, "id");
                                    if (user_id && user_id->valuestring) {
                                        // Compute SHA256 hash of user ID
                                        unsigned char hash[32];
                                        mbedtls_sha256((unsigned char *)user_id->valuestring, 
                                                      strlen(user_id->valuestring), hash, 0);
                                        
                                        // Convert hash to hex string
                                        for (int i = 0; i < 32; i++) {
                                            sprintf(&account_id[i * 2], "%02x", hash[i]);
                                        }
                                        account_id[64] = '\0';
                                        ESP_LOGI(TAG, "Account-Id computed");
                                    }
                                }
                                
                                logged_in = true;
                                ret = ESP_OK;
                                ESP_LOGI(TAG, "Login successful");
                                
                                // Save auth token and account_id to NVS for persistence
                                nvs_handle_t nvs_handle;
                                esp_err_t nvs_err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
                                if (nvs_err == ESP_OK) {
                                    nvs_err = nvs_set_str(nvs_handle, "auth_token", auth_token);
                                    if (nvs_err == ESP_OK) {
                                        nvs_err = nvs_set_str(nvs_handle, "account_id", account_id);
                                        if (nvs_err == ESP_OK) {
                                            nvs_commit(nvs_handle);
                                            ESP_LOGI(TAG, "Saved auth token to NVS (valid for ~6 months)");
                                        }
                                    }
                                    if (nvs_err != ESP_OK) {
                                        ESP_LOGW(TAG, "Failed to save auth token to NVS: %s", esp_err_to_name(nvs_err));
                                    }
                                    nvs_close(nvs_handle);
                                } else {
                                    ESP_LOGW(TAG, "Failed to open NVS for saving auth token: %s", esp_err_to_name(nvs_err));
                                }
                            }
                        }
                    }
                } else {
                    // Check for rate limiting (status 429)
                    if (status && status->valueint == 429) {
                        cJSON *data = cJSON_GetObjectItem(json, "data");
                        if (data) {
                            cJSON *lockout_data = cJSON_GetObjectItem(data, "data");
                            if (lockout_data) {
                                cJSON *lockout = cJSON_GetObjectItem(lockout_data, "lockout");
                                cJSON *failures = cJSON_GetObjectItem(lockout_data, "failures");
                                int lockout_seconds = lockout ? lockout->valueint : 0;
                                int failure_count = failures ? failures->valueint : 0;
                                ESP_LOGE(TAG, "Account locked due to too many login attempts!");
                                ESP_LOGE(TAG, "Failed attempts: %d, Lockout time: %d seconds (%d minutes)", 
                                        failure_count, lockout_seconds, lockout_seconds / 60);
                                ESP_LOGE(TAG, "Please wait before trying again.");
                            } else {
                                ESP_LOGE(TAG, "Rate limited (429): Account temporarily locked");
                            }
                        }
                    } else {
                        ESP_LOGE(TAG, "API returned error status: %d", status ? status->valueint : -1);
                    }
                }
                cJSON_Delete(json);
            } else {
                ESP_LOGE(TAG, "Failed to parse JSON response");
            }
        } else {
            ESP_LOGE(TAG, "HTTP request failed with status %d", status_code);
        }
    } else {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
    }
    
    esp_http_client_cleanup(client);
    free(post_data);
    
    return ret;
#endif
}

esp_err_t librelinkup_get_patient_id(char *patient_id, size_t patient_id_len)
{
#if DEMO_MODE_ENABLED
    ESP_LOGI(TAG, "[DEMO MODE] Returning dummy patient ID");
    strncpy(patient_id, "demo-patient-12345", patient_id_len - 1);
    patient_id[patient_id_len - 1] = '\0';
    return ESP_OK;
#else
    if (!logged_in) {
        ESP_LOGE(TAG, "Not logged in");
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t ret = ESP_FAIL;
    
    ESP_LOGI(TAG, "Getting patient connections...");
    
    // Reset response buffer
    http_response_len = 0;
    memset(http_response, 0, sizeof(http_response));
    
    // Configure HTTP client
    char url[128];
    snprintf(url, sizeof(url), "%s/llu/connections", api_url);
    
    ESP_LOGI(TAG, "Calling API: %s", url);
    
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .event_handler = http_event_handler,
        .timeout_ms = 10000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size_tx = 2048,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    
    // Set headers
    char auth_header[600];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", auth_token);
    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_header(client, "Account-Id", account_id);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "product", "llu.android");
    esp_http_client_set_header(client, "version", "4.16.0");
    esp_http_client_set_header(client, "Cache-Control", "no-cache");
    
    // Perform request with retry logic for DNS failures
    esp_err_t err = http_client_perform_with_retry(client, 3);
    
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "HTTP Status: %d, Response length: %d", status_code, http_response_len);
        
        if (status_code == 200) {
            // Parse JSON response
            cJSON *json = cJSON_Parse(http_response);
            if (json) {
                cJSON *status = cJSON_GetObjectItem(json, "status");
                if (status && status->valueint == 0) {
                    cJSON *data = cJSON_GetObjectItem(json, "data");
                    if (data && cJSON_IsArray(data)) {
                        cJSON *first_connection = cJSON_GetArrayItem(data, 0);
                        if (first_connection) {
                            cJSON *patient_id_obj = cJSON_GetObjectItem(first_connection, "patientId");
                            if (patient_id_obj && patient_id_obj->valuestring) {
                                strncpy(patient_id, patient_id_obj->valuestring, patient_id_len - 1);
                                patient_id[patient_id_len - 1] = '\0';
                                ret = ESP_OK;
                                ESP_LOGI(TAG, "Found patient ID: %s", patient_id);
                            }
                        }
                    }
                }
                cJSON_Delete(json);
            }
        }
    }
    
    esp_http_client_cleanup(client);
    return ret;
#endif
}

esp_err_t librelinkup_get_connections_json(char *json_buffer, size_t buffer_size)
{
#if DEMO_MODE_ENABLED
    ESP_LOGI(TAG, "[DEMO MODE] Returning dummy connections list");
    snprintf(json_buffer, buffer_size, 
             "{\"success\":true,\"patients\":[{\"id\":\"demo-patient-12345\",\"name\":\"Demo Patient\"}]}");
    return ESP_OK;
#else
    if (!logged_in) {
        ESP_LOGE(TAG, "Not logged in");
        snprintf(json_buffer, buffer_size, "{\"success\":false,\"error\":\"Not logged in\"}");
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t ret = ESP_FAIL;
    
    ESP_LOGI(TAG, "Getting patient connections for JSON...");
    
    // Reset response buffer
    http_response_len = 0;
    memset(http_response, 0, sizeof(http_response));
    
    // Configure HTTP client
    char url[128];
    snprintf(url, sizeof(url), "%s/llu/connections", api_url);
    
    ESP_LOGI(TAG, "Calling API: %s", url);
    
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .event_handler = http_event_handler,
        .timeout_ms = 10000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size_tx = 2048,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    
    // Set headers
    char auth_header[600];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", auth_token);
    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_header(client, "Account-Id", account_id);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "product", "llu.android");
    esp_http_client_set_header(client, "version", "4.16.0");
    esp_http_client_set_header(client, "Cache-Control", "no-cache");
    
    // Perform request
    esp_err_t err = esp_http_client_perform(client);
    
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "HTTP Status: %d, Response length: %d", status_code, http_response_len);
        ESP_LOGI(TAG, "Response: %s", http_response);
        
        if (status_code == 200) {
            // Parse JSON response
            cJSON *json = cJSON_Parse(http_response);
            if (json) {
                cJSON *status = cJSON_GetObjectItem(json, "status");
                if (status && status->valueint == 0) {
                    cJSON *data = cJSON_GetObjectItem(json, "data");
                    if (data && cJSON_IsArray(data)) {
                        // Build JSON response with patient list
                        cJSON *response = cJSON_CreateObject();
                        cJSON_AddTrueToObject(response, "success");
                        cJSON *patients_array = cJSON_CreateArray();
                        
                        int num_connections = cJSON_GetArraySize(data);
                        for (int i = 0; i < num_connections; i++) {
                            cJSON *connection = cJSON_GetArrayItem(data, i);
                            cJSON *patient_id = cJSON_GetObjectItem(connection, "patientId");
                            cJSON *first_name = cJSON_GetObjectItem(connection, "firstName");
                            cJSON *last_name = cJSON_GetObjectItem(connection, "lastName");
                            
                            if (patient_id && patient_id->valuestring) {
                                cJSON *patient = cJSON_CreateObject();
                                cJSON_AddStringToObject(patient, "id", patient_id->valuestring);
                                
                                // Build full name
                                char name[128] = {0};
                                if (first_name && first_name->valuestring) {
                                    strncpy(name, first_name->valuestring, sizeof(name) - 1);
                                }
                                if (last_name && last_name->valuestring) {
                                    if (name[0]) strncat(name, " ", sizeof(name) - strlen(name) - 1);
                                    strncat(name, last_name->valuestring, sizeof(name) - strlen(name) - 1);
                                }
                                if (name[0] == '\0') {
                                    snprintf(name, sizeof(name), "Patient %d", i + 1);
                                }
                                cJSON_AddStringToObject(patient, "name", name);
                                
                                cJSON_AddItemToArray(patients_array, patient);
                            }
                        }
                        
                        cJSON_AddItemToObject(response, "patients", patients_array);
                        
                        char *json_str = cJSON_PrintUnformatted(response);
                        if (json_str) {
                            strncpy(json_buffer, json_str, buffer_size - 1);
                            json_buffer[buffer_size - 1] = '\0';
                            free(json_str);
                            ret = ESP_OK;
                        }
                        cJSON_Delete(response);
                    }
                }
                cJSON_Delete(json);
            }
        }
    }
    
    if (ret != ESP_OK) {
        snprintf(json_buffer, buffer_size, "{\"success\":false,\"error\":\"Failed to get connections\"}");
    }
    
    esp_http_client_cleanup(client);
    return ret;
#endif
}

esp_err_t librelinkup_get_glucose(const char *patient_id, libre_glucose_data_t *glucose_data)
{
#if DEMO_MODE_ENABLED
    ESP_LOGI(TAG, "[DEMO MODE] Returning dummy glucose data");
    // Dummy data from https://gist.github.com/khskekec/6c13ba01b10d3018d816706a32ae8ab2
    glucose_data->value_mgdl = 97;  // ValueInMgPerDl from dummy data
    glucose_data->value_mmol = 97.0 / 18.0;  // Convert to mmol/L
    glucose_data->trend = LIBRE_TREND_STABLE;  // TrendArrow: 3
    glucose_data->is_high = false;
    glucose_data->is_low = false;
    strncpy(glucose_data->timestamp, "2023-03-01T12:34:56.000Z", sizeof(glucose_data->timestamp) - 1);
    return ESP_OK;
#else
    if (!logged_in) {
        ESP_LOGE(TAG, "Not logged in");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!patient_id || !glucose_data) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret = ESP_FAIL;
    
    ESP_LOGI(TAG, "Getting glucose data for patient: %s", patient_id);
    
    // Reset response buffer
    http_response_len = 0;
    memset(http_response, 0, sizeof(http_response));
    
    // Configure HTTP client
    char url[192];
    snprintf(url, sizeof(url), "%s/llu/connections/%s/graph", api_url, patient_id);
    
    ESP_LOGI(TAG, "Calling API: %s", url);
    
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .event_handler = http_event_handler,
        .timeout_ms = 10000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size_tx = 2048,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    
    // Set headers
    char auth_header[600];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", auth_token);
    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_header(client, "Account-Id", account_id);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "product", "llu.android");
    esp_http_client_set_header(client, "version", "4.16.0");
    esp_http_client_set_header(client, "Cache-Control", "no-cache");
    
    // Perform request with retry logic for DNS failures
    esp_err_t err = http_client_perform_with_retry(client, 3);
    
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "HTTP Status: %d, Response length: %d", status_code, http_response_len);
        
        if (status_code == 200) {
            // The /graph endpoint returns a lot of data (~11KB) which can cause cJSON to run out of memory
            // We only need the glucoseMeasurement field, so let's extract just that portion
            const char *glucose_key = "\"glucoseMeasurement\":";
            char *glucose_start = strstr(http_response, glucose_key);
            
            if (glucose_start) {
                glucose_start += strlen(glucose_key);
                // Find the end of the glucoseMeasurement object (look for the matching closing brace)
                int brace_count = 0;
                char *glucose_end = glucose_start;
                bool in_string = false;
                bool escape_next = false;
                
                while (*glucose_end) {
                    if (escape_next) {
                        escape_next = false;
                    } else if (*glucose_end == '\\') {
                        escape_next = true;
                    } else if (*glucose_end == '"') {
                        in_string = !in_string;
                    } else if (!in_string) {
                        if (*glucose_end == '{') brace_count++;
                        else if (*glucose_end == '}') {
                            if (brace_count == 0) break;
                            brace_count--;
                        }
                    }
                    glucose_end++;
                }
                
                if (*glucose_end == '}') {
                    glucose_end++; // Include the closing brace
                    size_t glucose_len = glucose_end - glucose_start;
                    char glucose_json[2048];
                    
                    if (glucose_len < sizeof(glucose_json)) {
                        memcpy(glucose_json, glucose_start, glucose_len);
                        glucose_json[glucose_len] = '\0';
                        
                        ESP_LOGI(TAG, "Extracted glucoseMeasurement JSON (%d bytes)", glucose_len);
                        
                        // Parse just the glucoseMeasurement object
                        cJSON *glucose_measurement = cJSON_Parse(glucose_json);
                        if (glucose_measurement) {
                            // Extract glucose data
                            cJSON *value = cJSON_GetObjectItem(glucose_measurement, "ValueInMgPerDl");
                            cJSON *trend = cJSON_GetObjectItem(glucose_measurement, "TrendArrow");
                            cJSON *is_high = cJSON_GetObjectItem(glucose_measurement, "isHigh");
                            cJSON *is_low = cJSON_GetObjectItem(glucose_measurement, "isLow");
                            cJSON *timestamp = cJSON_GetObjectItem(glucose_measurement, "Timestamp");
                            cJSON *measurement_color = cJSON_GetObjectItem(glucose_measurement, "MeasurementColor");
                            cJSON *type = cJSON_GetObjectItem(glucose_measurement, "type");
                            
                            // Log all values in one line
                            ESP_LOGI(TAG, "Glucose Data: Value=%d, Trend=%d, isHigh=%s, isLow=%s, Color=%d, Type=%d, Time=%s",
                                value && value->type == cJSON_Number ? value->valueint : -1,
                                trend && trend->type == cJSON_Number ? trend->valueint : -1,
                                is_high ? (cJSON_IsTrue(is_high) ? "true" : "false") : "NULL",
                                is_low ? (cJSON_IsTrue(is_low) ? "true" : "false") : "NULL",
                                measurement_color && measurement_color->type == cJSON_Number ? measurement_color->valueint : -1,
                                type && type->type == cJSON_Number ? type->valueint : -1,
                                timestamp && timestamp->valuestring ? timestamp->valuestring : "NULL");
                            
                            if (value && trend) {
                                glucose_data->value_mgdl = value->valueint;
                                glucose_data->value_mmol = value->valueint / 18.0;
                                glucose_data->trend = (libre_trend_t)trend->valueint;
                                glucose_data->is_high = is_high ? cJSON_IsTrue(is_high) : false;
                                glucose_data->is_low = is_low ? cJSON_IsTrue(is_low) : false;
                                glucose_data->measurement_color = measurement_color ? measurement_color->valueint : 0;
                                glucose_data->type = type ? type->valueint : 0;
                                
                                if (timestamp && timestamp->valuestring) {
                                    // Parse timestamp format: "5/21/2022 3:38:50 PM" and convert to dd/mm/yyyy HH:MM:SS
                                    int year, month, day, hour, minute, second;
                                    char ampm[3];
                                    if (sscanf(timestamp->valuestring, "%d/%d/%d %d:%d:%d %2s", &month, &day, &year, &hour, &minute, &second, ampm) == 7) {
                                        // Convert 12-hour to 24-hour format
                                        if (strcmp(ampm, "PM") == 0 && hour != 12) {
                                            hour += 12;
                                        } else if (strcmp(ampm, "AM") == 0 && hour == 12) {
                                            hour = 0;
                                        }
                                        // Format as dd/mm/yyyy HH:MM:SS
                                        snprintf(glucose_data->timestamp, sizeof(glucose_data->timestamp), "%02d/%02d/%d %02d:%02d:%02d", day, month, year, hour, minute, second);
                                    } else {
                                        strncpy(glucose_data->timestamp, "Unknown", sizeof(glucose_data->timestamp) - 1);
                                    }
                                } else {
                                    strncpy(glucose_data->timestamp, "Unknown", sizeof(glucose_data->timestamp) - 1);
                                }
                                
                                ret = ESP_OK;
                                ESP_LOGI(TAG, "Glucose: %d mg/dL, Trend: %d, High: %d, Low: %d",
                                         glucose_data->value_mgdl, glucose_data->trend,
                                         glucose_data->is_high, glucose_data->is_low);
                                
                                // Parse graphData array for historical values
                                const char *graph_key = "\"graphData\":";
                                char *graph_start = strstr(http_response, graph_key);
                                if (graph_start) {
                                    graph_start += strlen(graph_key);
                                    // Skip whitespace and opening bracket
                                    while (*graph_start && (*graph_start == ' ' || *graph_start == '\n' || *graph_start == '\t')) graph_start++;
                                    if (*graph_start == '[') {
                                        graph_start++;
                                        cached_graph_data.count = 0;
                                        
                                        // Parse array items one by one
                                        char *item_start = graph_start;
                                        while (cached_graph_data.count < MAX_GRAPH_POINTS && *item_start) {
                                            // Skip to next object
                                            while (*item_start && *item_start != '{') item_start++;
                                            if (*item_start != '{') break;
                                            
                                            // Find end of object
                                            int braces = 0;
                                            char *item_end = item_start;
                                            bool in_str = false;
                                            while (*item_end) {
                                                if (*item_end == '"' && (item_end == item_start || *(item_end-1) != '\\')) in_str = !in_str;
                                                if (!in_str) {
                                                    if (*item_end == '{') braces++;
                                                    else if (*item_end == '}') {
                                                        braces--;
                                                        if (braces == 0) break;
                                                    }
                                                }
                                                item_end++;
                                            }
                                            
                                            if (braces == 0 && *item_end == '}') {
                                                item_end++;
                                                size_t item_len = item_end - item_start;
                                                char item_json[256];
                                                if (item_len < sizeof(item_json)) {
                                                    memcpy(item_json, item_start, item_len);
                                                    item_json[item_len] = '\0';
                                                    
                                                    cJSON *item = cJSON_Parse(item_json);
                                                    if (item) {
                                                        cJSON *val = cJSON_GetObjectItem(item, "ValueInMgPerDl");
                                                        cJSON *color = cJSON_GetObjectItem(item, "MeasurementColor");
                                                        if (val && val->type == cJSON_Number) {
                                                            cached_graph_data.points[cached_graph_data.count].value_mmol = val->valueint / 18.0f;
                                                            cached_graph_data.points[cached_graph_data.count].measurement_color = 
                                                                (color && color->type == cJSON_Number) ? color->valueint : 1;
                                                            cached_graph_data.count++;
                                                        }
                                                        cJSON_Delete(item);
                                                    }
                                                }
                                                item_start = item_end;
                                            } else {
                                                break;
                                            }
                                        }
                                        ESP_LOGI(TAG, "Parsed %d graph data points", cached_graph_data.count);
                                    }
                                }
                            } else {
                                ESP_LOGE(TAG, "Missing required glucose fields (value or trend)");
                            }
                            cJSON_Delete(glucose_measurement);
                        } else {
                            ESP_LOGE(TAG, "Failed to parse glucoseMeasurement JSON");
                        }
                    } else {
                        ESP_LOGE(TAG, "glucoseMeasurement too large (%d bytes)", glucose_len);
                    }
                } else {
                    ESP_LOGE(TAG, "Could not find end of glucoseMeasurement object");
                }
            } else {
                ESP_LOGE(TAG, "glucoseMeasurement not found in response");
            }
        } else if (status_code == 401) {
            ESP_LOGE(TAG, "Authentication failed (401) - token may be expired");
            ret = ESP_ERR_LIBRE_AUTH_FAILED;
        }
    }
    
    esp_http_client_cleanup(client);
    return ret;
#endif
}

bool librelinkup_is_logged_in(void)
{
    return logged_in;
}

void librelinkup_logout(void)
{
    memset(auth_token, 0, sizeof(auth_token));
    memset(account_id, 0, sizeof(account_id));
    logged_in = false;
    
    // Clear auth token from NVS
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        nvs_erase_key(nvs_handle, "auth_token");
        nvs_erase_key(nvs_handle, "account_id");
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
        ESP_LOGI(TAG, "Logged out and cleared saved auth token");
    } else {
        ESP_LOGI(TAG, "Logged out");
    }
}

const char* librelinkup_get_trend_string(libre_trend_t trend)
{
    switch (trend) {
        case LIBRE_TREND_RISING_QUICKLY:
            return "↑↑";
        case LIBRE_TREND_RISING:
            return "↑";
        case LIBRE_TREND_STABLE:
            return "→";
        case LIBRE_TREND_FALLING:
            return "↓";
        case LIBRE_TREND_FALLING_QUICKLY:
            return "↓↓";
        case LIBRE_TREND_NONE:
        default:
            return "*";  // Use star for unknown/no data instead of ?
    }
}

float librelinkup_mgdl_to_mmol(int mgdl)
{
    return mgdl / 18.0;
}

esp_err_t librelinkup_get_graph_data(libre_graph_data_t *graph_data)
{
    if (!graph_data) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (cached_graph_data.count == 0) {
        return ESP_ERR_NOT_FOUND;
    }
    
    memcpy(graph_data, &cached_graph_data, sizeof(libre_graph_data_t));
    return ESP_OK;
}
