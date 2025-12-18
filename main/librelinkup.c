/**
 * LibreLinkUp API Client Implementation
 */

#include "librelinkup.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_tls.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "LIBRELINKUP";

// Configuration
static char api_url[64] = LIBRELINKUP_API_URL_GLOBAL;
static char auth_token[512] = {0};
static bool logged_in = false;

// HTTP response buffer
#define HTTP_BUFFER_SIZE 8192
static char http_response[HTTP_BUFFER_SIZE];
static int http_response_len = 0;

/**
 * HTTP event handler
 */
static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (http_response_len + evt->data_len < HTTP_BUFFER_SIZE) {
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

esp_err_t librelinkup_init(bool use_eu_server)
{
    if (use_eu_server) {
        strncpy(api_url, LIBRELINKUP_API_URL_EU, sizeof(api_url) - 1);
    } else {
        strncpy(api_url, LIBRELINKUP_API_URL_GLOBAL, sizeof(api_url) - 1);
    }
    
    ESP_LOGI(TAG, "Initialized with API URL: %s", api_url);
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
    
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .event_handler = http_event_handler,
        .timeout_ms = 10000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    
    // Set headers
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "product", "llu.android");
    esp_http_client_set_header(client, "version", "4.2.1");
    esp_http_client_set_header(client, "Accept-Encoding", "gzip");
    esp_http_client_set_header(client, "Cache-Control", "no-cache");
    esp_http_client_set_header(client, "Connection", "Keep-Alive");
    
    esp_http_client_set_post_field(client, post_data, strlen(post_data));
    
    // Perform request
    esp_err_t err = esp_http_client_perform(client);
    
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "HTTP Status: %d, Response length: %d", status_code, http_response_len);
        
        if (status_code == 200) {
            // Parse JSON response
            cJSON *json = cJSON_Parse(http_response);
            if (json) {
                cJSON *status = cJSON_GetObjectItem(json, "status");
                if (status && status->valueint == 0) {
                    // Extract auth token
                    cJSON *data = cJSON_GetObjectItem(json, "data");
                    if (data) {
                        cJSON *auth_ticket = cJSON_GetObjectItem(data, "authTicket");
                        if (auth_ticket) {
                            cJSON *token = cJSON_GetObjectItem(auth_ticket, "token");
                            if (token && token->valuestring) {
                                strncpy(auth_token, token->valuestring, sizeof(auth_token) - 1);
                                logged_in = true;
                                ret = ESP_OK;
                                ESP_LOGI(TAG, "Login successful");
                            }
                        }
                    }
                } else {
                    ESP_LOGE(TAG, "API returned error status");
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
    
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .event_handler = http_event_handler,
        .timeout_ms = 10000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    
    // Set headers
    char auth_header[600];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", auth_token);
    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "product", "llu.android");
    esp_http_client_set_header(client, "version", "4.2.1");
    esp_http_client_set_header(client, "Accept-Encoding", "gzip");
    esp_http_client_set_header(client, "Cache-Control", "no-cache");
    
    // Perform request
    esp_err_t err = esp_http_client_perform(client);
    
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
    
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .event_handler = http_event_handler,
        .timeout_ms = 10000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    
    // Set headers
    char auth_header[600];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", auth_token);
    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "product", "llu.android");
    esp_http_client_set_header(client, "version", "4.2.1");
    esp_http_client_set_header(client, "Accept-Encoding", "gzip");
    esp_http_client_set_header(client, "Cache-Control", "no-cache");
    
    // Perform request
    esp_err_t err = esp_http_client_perform(client);
    
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
    
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .event_handler = http_event_handler,
        .timeout_ms = 10000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    
    // Set headers
    char auth_header[600];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", auth_token);
    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "product", "llu.android");
    esp_http_client_set_header(client, "version", "4.2.1");
    esp_http_client_set_header(client, "Accept-Encoding", "gzip");
    esp_http_client_set_header(client, "Cache-Control", "no-cache");
    
    // Perform request
    esp_err_t err = esp_http_client_perform(client);
    
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
                    if (data) {
                        cJSON *connection = cJSON_GetObjectItem(data, "connection");
                        if (connection) {
                            cJSON *glucose_measurement = cJSON_GetObjectItem(connection, "glucoseMeasurement");
                            if (glucose_measurement) {
                                // Extract glucose data
                                cJSON *value = cJSON_GetObjectItem(glucose_measurement, "ValueInMgPerDl");
                                cJSON *trend = cJSON_GetObjectItem(glucose_measurement, "TrendArrow");
                                cJSON *is_high = cJSON_GetObjectItem(glucose_measurement, "isHigh");
                                cJSON *is_low = cJSON_GetObjectItem(glucose_measurement, "isLow");
                                cJSON *timestamp = cJSON_GetObjectItem(glucose_measurement, "Timestamp");
                                
                                if (value && trend) {
                                    glucose_data->value_mgdl = value->valueint;
                                    glucose_data->value_mmol = value->valueint / 18.0;
                                    glucose_data->trend = (libre_trend_t)trend->valueint;
                                    glucose_data->is_high = is_high ? cJSON_IsTrue(is_high) : false;
                                    glucose_data->is_low = is_low ? cJSON_IsTrue(is_low) : false;
                                    
                                    if (timestamp && timestamp->valuestring) {
                                        strncpy(glucose_data->timestamp, timestamp->valuestring, sizeof(glucose_data->timestamp) - 1);
                                    } else {
                                        strncpy(glucose_data->timestamp, "Unknown", sizeof(glucose_data->timestamp) - 1);
                                    }
                                    
                                    ret = ESP_OK;
                                    ESP_LOGI(TAG, "Glucose: %d mg/dL, Trend: %d, High: %d, Low: %d",
                                             glucose_data->value_mgdl, glucose_data->trend,
                                             glucose_data->is_high, glucose_data->is_low);
                                }
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

bool librelinkup_is_logged_in(void)
{
    return logged_in;
}

void librelinkup_logout(void)
{
    memset(auth_token, 0, sizeof(auth_token));
    logged_in = false;
    ESP_LOGI(TAG, "Logged out");
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
