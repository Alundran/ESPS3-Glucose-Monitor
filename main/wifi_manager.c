/**
 * WiFi Manager for ESP32-S3-BOX-3
 */

#include "wifi_manager.h"
#include "config.h"
#include "libre_credentials.h"
#include "librelinkup.h"
#include "global_settings.h"
#include "ota_update.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include <string.h>

static const char *TAG = "WIFI_MANAGER";

// WiFi credentials storage key
#define WIFI_NAMESPACE "wifi_config"
#define WIFI_SSID_KEY "ssid"
#define WIFI_PASS_KEY "password"

// AP mode configuration
#define AP_SSID WIFI_AP_SSID
#define AP_PASS WIFI_AP_PASSWORD
#define AP_MAX_CONN 4

static bool wifi_connected = false;
static bool wifi_initialized = false;
static esp_netif_t *sta_netif = NULL;
static esp_netif_t *ap_netif = NULL;
static char current_ssid[33] = {0};
static char current_ip[16] = {0};
static wifi_connected_cb_t connected_callback = NULL;
static wifi_disconnected_cb_t disconnected_callback = NULL;
static wifi_failed_cb_t failed_callback = NULL;
static httpd_handle_t server = NULL;
static int retry_count = 0;
static const int max_retry_attempts = 5;

// HTML for the main menu page
static const char* html_main = 
"<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'>"
"<style>"
"body{font-family:Arial;text-align:center;margin:20px;background:#1a1a1a;color:#fff;}"
"h1{color:#4CAF50;}"
"button{padding:15px 30px;margin:15px;font-size:18px;width:80%;max-width:300px;border-radius:8px;border:none;background:#4CAF50;color:white;cursor:pointer;display:block;margin-left:auto;margin-right:auto;}"
"button:hover{background:#45a049;}"
".info{margin:20px;color:#888;}"
"</style>"
"</head><body><h1>" DEVICE_NAME "</h1>"
"<button onclick=\"location.href='/wifi'\">Configure WiFi</button>"
"<button onclick=\"location.href='/librelink'\">Configure LibreLink</button>"
"<button onclick=\"location.href='/settings'\">Global Settings</button>"
"<div class='info'>Firmware v" DEVICE_VERSION "</div></body></html>";

// HTML for WiFi setup page
static const char* html_wifi = 
"<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'>"
"<style>"
"body{font-family:Arial;text-align:center;margin:20px;background:#1a1a1a;color:#fff;}"
"h1{color:#4CAF50;}"
"input,button,select{padding:12px;margin:8px;font-size:16px;width:80%;max-width:300px;border-radius:5px;border:none;display:block;margin-left:auto;margin-right:auto;box-sizing:border-box;}"
"button{background:#4CAF50;color:white;cursor:pointer;}button:hover{background:#45a049;}"
"select{background:#333;color:#fff;}"
".loading{margin:10px auto;}"
".back{background:#666;margin-top:30px;}"
"</style>"
"<script>"
"function scanNetworks(){document.getElementById('scan-btn').style.display='none';document.getElementById('loading').innerHTML='Scanning...';fetch('/scan').then(r=>r.json()).then(d=>{let s=document.getElementById('ssid-select');s.innerHTML='<option value=\"\">Select Network...</option>';d.forEach(n=>s.innerHTML+='<option value=\"'+n+'\">'+n+'</option>');s.style.display='block';document.getElementById('loading').innerHTML='';}).catch(e=>{alert('Scan failed: '+e);document.getElementById('scan-btn').style.display='block';document.getElementById('loading').innerHTML='';});}"
"function selectSSID(){document.getElementById('ssid').value=document.getElementById('ssid-select').value;}"
"</script>"
"</head><body><h1>WiFi Setup</h1>"
"<p>Connect your device to WiFi</p>"
"<button id='scan-btn' onclick='scanNetworks()'>Scan for Networks</button>"
"<div id='loading' class='loading'></div>"
"<select id='ssid-select' onchange='selectSSID()' style='display:none'></select>"
"<form action='/save' method='post'>"
"<input id='ssid' name='ssid' placeholder='WiFi SSID (or scan above)' required><br>"
"<input name='pass' type='password' placeholder='Password' required><br>"
"<button type='submit'>Connect</button></form>"
"<button class='back' onclick=\"location.href='/'\">Back to Menu</button></body></html>";

// HTML for LibreLink setup page
static const char* html_librelink = 
"<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'>"
"<style>"
"body{font-family:Arial;text-align:center;margin:20px;background:#1a1a1a;color:#fff;}"
"h1{color:#4CAF50;}"
"input,button,select{padding:12px;margin:8px;font-size:16px;width:80%;max-width:300px;border-radius:5px;border:none;display:block;margin-left:auto;margin-right:auto;box-sizing:border-box;}"
"button{background:#4CAF50;color:white;cursor:pointer;}button:hover{background:#45a049;}"
"select{background:#333;color:#fff;}"
".back{background:#666;margin-top:30px;}"
".loading{color:#888;margin:10px;}"
".error{color:#ff4444;margin:10px;}"
".load-btn{background:#2196F3;margin-top:5px;}"
".load-btn:hover{background:#0b7dda;}"
"#patient-group{display:none;}"
"</style>"
"<script>"
"function loadPatients(){"
"  const email=document.getElementById('email').value;"
"  const password=document.getElementById('password').value;"
"  const server=document.getElementById('server').value;"
"  if(!email||!password){alert('Please enter email and password first');return;}"
"  document.getElementById('load-btn').style.display='none';"
"  document.getElementById('loading').style.display='block';"
"  document.getElementById('error').style.display='none';"
"  fetch('/libre/patients?email='+encodeURIComponent(email)+'&pass='+encodeURIComponent(password)+'&server='+server)"
"  .then(r=>r.json()).then(d=>{"
"    document.getElementById('loading').style.display='none';"
"    if(d.success){"
"      let sel=document.getElementById('patient-select');"
"      sel.innerHTML='<option value=\"\">Select Patient...</option>';"
"      d.patients.forEach(p=>sel.innerHTML+='<option value=\"'+p.id+'\">'+p.name+'</option>');"
"      document.getElementById('patient-group').style.display='block';"
"      document.getElementById('load-btn').style.display='block';"
"    }else{"
"      document.getElementById('error').textContent='Login failed: '+(d.error||'Unknown error');"
"      document.getElementById('error').style.display='block';"
"      document.getElementById('load-btn').style.display='block';"
"    }"
"  }).catch(e=>{"
"    document.getElementById('loading').style.display='none';"
"    document.getElementById('error').textContent='Error: '+e;"
"    document.getElementById('error').style.display='block';"
"    document.getElementById('load-btn').style.display='block';"
"  });"
"}"
"function selectPatient(){"
"  document.getElementById('patient_id').value=document.getElementById('patient-select').value;"
"}"
"</script>"
"</head><body><h1>LibreLink Setup</h1>"
"<p>Configure your LibreLinkUp credentials</p>"
"<form action='/libre/save' method='post'>"
"<input id='email' name='email' type='email' placeholder='LibreLink Email' required><br>"
"<input id='password' name='password' type='password' placeholder='LibreLink Password' required><br>"
"<select id='server' name='server'><option value='0'>Global Server</option><option value='1'>EU Server</option></select>"
"<button type='button' id='load-btn' class='load-btn' onclick='loadPatients()'>Load Patient(s)</button>"
"<div id='loading' class='loading' style='display:none'>Loading patients...</div>"
"<div id='error' class='error' style='display:none'></div>"
"<div id='patient-group'>"
"<select id='patient-select' onchange='selectPatient()'></select>"
"</div>"
"<input id='patient_id' name='patient_id' type='hidden'><br>"
"<button type='submit'>Save Credentials</button></form>"
"<button class='back' onclick=\"location.href='/'\">Back to Menu</button></body></html>";

// HTML for Global Settings page
static const char* html_settings = 
"<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'>"
"<style>"
"body{font-family:Arial;text-align:center;margin:20px;background:#1a1a1a;color:#fff;}"
"h1{color:#4CAF50;}"
"h2{color:#888;font-size:18px;margin-top:30px;margin-bottom:10px;text-align:left;max-width:300px;margin-left:auto;margin-right:auto;}"
"input,button,select{padding:12px;margin:8px;font-size:16px;width:80%;max-width:300px;border-radius:5px;border:none;display:block;margin-left:auto;margin-right:auto;box-sizing:border-box;}"
"button{background:#4CAF50;color:white;cursor:pointer;}button:hover{background:#45a049;}"
".back{background:#666;margin-top:30px;}"
".update-btn{background:#ff9800;}"
".form-row{max-width:300px;margin:15px auto;text-align:left;}"
".form-row label{display:block;margin-bottom:5px;color:#bbb;}"
".toggle-container{display:flex;align-items:center;justify-content:space-between;max-width:300px;margin:15px auto;padding:12px;background:#2a2a2a;border-radius:5px;}"
".toggle-container label{color:#bbb;margin:0;}"
".switch{position:relative;display:inline-block;width:50px;height:24px;}"
".switch input{opacity:0;width:0;height:0;}"
".slider{position:absolute;cursor:pointer;top:0;left:0;right:0;bottom:0;background-color:#666;transition:.4s;border-radius:24px;}"
".slider:before{position:absolute;content:\"\";height:16px;width:16px;left:4px;bottom:4px;background-color:white;transition:.4s;border-radius:50%;}"
"input:checked + .slider{background-color:#4CAF50;}"
"input:checked + .slider:before{transform:translateX(26px);}"
".info{color:#888;font-size:12px;margin:5px auto;max-width:300px;text-align:left;}"
"#updateMsg{margin:10px;color:#ff9800;min-height:20px;}"
"</style>"
"<script>"
"function loadSettings(){"
"  fetch('/settings/load').then(r=>r.json()).then(d=>{"
"    if(d.success){"
"      document.getElementById('interval').value=d.interval;"
"      document.getElementById('moon_lamp').checked=d.moon_lamp;"
"    }"
"  }).catch(e=>console.error('Failed to load settings:',e));"
"}"
"function checkUpdate(){"
"  const btn=document.getElementById('updateBtn');"
"  const msg=document.getElementById('updateMsg');"
"  btn.disabled=true;"
"  btn.innerText='Checking...';"
"  msg.innerText='Checking for updates...';"
"  fetch('/ota/check').then(r=>r.json()).then(d=>{"
"    if(d.updateAvailable){"
"      msg.innerText='Update available: '+d.currentVersion+' → '+d.newVersion;"
"      if(confirm('Update available! Current: '+d.currentVersion+', New: '+d.newVersion+'\\n\\nWARNING: Do NOT disconnect power during update!\\n\\nProceed?')){"
"        msg.innerText='Starting update...';"
"        fetch('/ota/update',{method:'POST'}).then(()=>{"
"          msg.innerText='Update in progress... Device will reboot when complete.';"
"        });"
"      }else{"
"        btn.disabled=false;btn.innerText='Check for Updates';"
"      }"
"    }else if(d.error){"
"      msg.innerText='Error: '+d.error;"
"      btn.disabled=false;btn.innerText='Check for Updates';"
"    }else{"
"      msg.innerText='Already running latest version: '+d.currentVersion;"
"      btn.disabled=false;btn.innerText='Check for Updates';"
"    }"
"  }).catch(e=>{msg.innerText='Failed to check for updates';btn.disabled=false;btn.innerText='Check for Updates';});"
"}"
"window.onload=loadSettings;"
"</script>"
"</head><body><h1>Global Settings</h1>"
"<form action='/settings/save' method='post'>"
"<h2>LibreLink Configuration</h2>"
"<div class='form-row'>"
"<label for='interval'>Update Interval (minutes)</label>"
"<input id='interval' name='interval' type='number' min='1' value='5' required>"
"<div class='info'>How often to fetch glucose data (minimum 1 minute)</div>"
"</div>"
"<h2>Moon Lamp Control</h2>"
"<div class='toggle-container'>"
"<label for='moon_lamp'>Enable Moon Lamp</label>"
"<label class='switch'>"
"<input id='moon_lamp' name='moon_lamp' type='checkbox' value='1'>"
"<span class='slider'></span>"
"</label>"
"</div>"
"<div class='info' style='text-align:center;margin-top:5px;'>Control Moon Lamp via IR based on glucose levels</div>"
"<button type='submit' style='margin-top:30px;'>Save Settings</button></form>"
"<h2 style='text-align:center;'>Firmware Update</h2>"
"<button id='updateBtn' class='update-btn' onclick='checkUpdate()'>Check for Updates</button>"
"<div id='updateMsg'></div>"
"<button class='back' onclick=\"location.href='/'\">Back to Menu</button></body></html>";

static const char* success_page = 
"<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'>"
"<style>body{font-family:Arial;text-align:center;margin:50px;}</style>"
"</head><body><h1>Success!</h1><p>WiFi credentials saved. Device will now restart and connect.</p></body></html>";

// HTTP GET handler for root - main menu
static esp_err_t root_get_handler(httpd_req_t *req) {
    httpd_resp_send(req, html_main, strlen(html_main));
    return ESP_OK;
}

// HTTP GET handler for WiFi setup page
static esp_err_t wifi_get_handler(httpd_req_t *req) {
    httpd_resp_send(req, html_wifi, strlen(html_wifi));
    return ESP_OK;
}

// HTTP GET handler for LibreLink setup page
static esp_err_t librelink_get_handler(httpd_req_t *req) {
    httpd_resp_send(req, html_librelink, strlen(html_librelink));
    return ESP_OK;
}

// HTTP POST handler for saving credentials
static esp_err_t save_post_handler(httpd_req_t *req) {
    char buf[200];
    char ssid[33] = {0};
    char pass[65] = {0};
    int ret, remaining = req->content_len;
    
    // Read POST data
    if (remaining < sizeof(buf)) {
        ret = httpd_req_recv(req, buf, remaining);
        if (ret <= 0) {
            return ESP_FAIL;
        }
        buf[ret] = '\0';
        
        // Parse form data
        char *ssid_start = strstr(buf, "ssid=");
        char *pass_start = strstr(buf, "pass=");
        
        if (ssid_start && pass_start) {
            ssid_start += 5;
            char *ssid_end = strchr(ssid_start, '&');
            if (ssid_end) {
                int ssid_len = ssid_end - ssid_start;
                if (ssid_len < sizeof(ssid)) {
                    strncpy(ssid, ssid_start, ssid_len);
                    ssid[ssid_len] = '\0';
                }
            }
            
            pass_start += 5;
            char *pass_end = strchr(pass_start, '&');
            int pass_len = pass_end ? (pass_end - pass_start) : strlen(pass_start);
            if (pass_len < sizeof(pass)) {
                strncpy(pass, pass_start, pass_len);
                pass[pass_len] = '\0';
            }
            
            // Save to NVS
            nvs_handle_t nvs_handle;
            if (nvs_open(WIFI_NAMESPACE, NVS_READWRITE, &nvs_handle) == ESP_OK) {
                nvs_set_str(nvs_handle, WIFI_SSID_KEY, ssid);
                nvs_set_str(nvs_handle, WIFI_PASS_KEY, pass);
                nvs_commit(nvs_handle);
                nvs_close(nvs_handle);
                
                ESP_LOGI(TAG, "WiFi credentials saved: %s", ssid);
                
                httpd_resp_send(req, success_page, strlen(success_page));
                
                // Restart after a short delay
                vTaskDelay(pdMS_TO_TICKS(2000));
                esp_restart();
                
                return ESP_OK;
            }
        }
    }
    
    httpd_resp_send_500(req);
    return ESP_FAIL;
}

// HTTP POST handler for saving LibreLink credentials
static esp_err_t libre_save_post_handler(httpd_req_t *req) {
    char buf[512];
    char email[128] = {0};
    char password[128] = {0};
    char patient_id[64] = {0};
    bool use_eu_server = false;
    int ret, remaining = req->content_len;
    
    if (remaining < sizeof(buf)) {
        ret = httpd_req_recv(req, buf, remaining);
        if (ret <= 0) {
            return ESP_FAIL;
        }
        buf[ret] = '\0';
        
        // Parse form data
        char *email_start = strstr(buf, "email=");
        char *pass_start = strstr(buf, "password=");
        char *patient_start = strstr(buf, "patient_id=");
        char *server_start = strstr(buf, "server=");
        
        if (email_start && pass_start) {
            // Parse email
            email_start += 6;
            char *email_end = strchr(email_start, '&');
            if (email_end) {
                int len = email_end - email_start;
                if (len < sizeof(email)) {
                    strncpy(email, email_start, len);
                    email[len] = '\0';
                }
            }
            
            // Parse password
            pass_start += 9;
            char *pass_end = strchr(pass_start, '&');
            if (pass_end) {
                int len = pass_end - pass_start;
                if (len < sizeof(password)) {
                    strncpy(password, pass_start, len);
                    password[len] = '\0';
                }
            } else {
                strncpy(password, pass_start, sizeof(password) - 1);
            }
            
            // Parse patient ID (optional)
            if (patient_start) {
                patient_start += 11;
                char *patient_end = strchr(patient_start, '&');
                int len = patient_end ? (patient_end - patient_start) : strlen(patient_start);
                if (len > 0 && len < sizeof(patient_id)) {
                    strncpy(patient_id, patient_start, len);
                    patient_id[len] = '\0';
                }
            }
            
            // Parse server selection
            if (server_start) {
                use_eu_server = (*(server_start + 7) == '1');
            }
            
            // Save credentials
            esp_err_t err = libre_credentials_save(email, password, 
                                                    patient_id[0] ? patient_id : NULL, 
                                                    use_eu_server);
            
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "LibreLink credentials saved");
                httpd_resp_send(req, success_page, strlen(success_page));
                return ESP_OK;
            }
        }
    }
    
    const char* error_page = "<html><body><h1>Error saving credentials</h1></body></html>";
    httpd_resp_send(req, error_page, strlen(error_page));
    return ESP_OK;
}

// HTTP GET handler for loading LibreLink patients
static esp_err_t libre_patients_get_handler(httpd_req_t *req) {
    char buf[512];
    char email[128] = {0};
    char password[128] = {0};
    bool use_eu_server = false;
    
    // Parse query string
    size_t buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1 && buf_len < sizeof(buf)) {
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            char param[128];
            
            // Get email
            if (httpd_query_key_value(buf, "email", param, sizeof(param)) == ESP_OK) {
                strncpy(email, param, sizeof(email) - 1);
            }
            
            // Get password
            if (httpd_query_key_value(buf, "pass", param, sizeof(param)) == ESP_OK) {
                strncpy(password, param, sizeof(password) - 1);
            }
            
            // Get server
            if (httpd_query_key_value(buf, "server", param, sizeof(param)) == ESP_OK) {
                use_eu_server = (param[0] == '1');
            }
        }
    }
    
    // Test connection and get connections list
    librelinkup_init(use_eu_server);
    esp_err_t err = librelinkup_login(email, password);
    
    if (err == ESP_OK) {
        // Get connections list with patient info
        char response[2048];
        err = librelinkup_get_connections_json(response, sizeof(response));
        
        if (err == ESP_OK) {
            httpd_resp_set_type(req, "application/json");
            httpd_resp_send(req, response, strlen(response));
            librelinkup_logout();
            return ESP_OK;
        }
    }
    
    // Return error
    const char* error_response = "{\"success\":false,\"error\":\"Login failed or no patients found\"}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, error_response, strlen(error_response));
    librelinkup_logout();
    return ESP_OK;
}

// HTTP GET handler for testing LibreLink connection (deprecated - use /libre/patients)
static esp_err_t libre_test_get_handler(httpd_req_t *req) {
    char buf[512];
    char email[128] = {0};
    char password[128] = {0};
    bool use_eu_server = false;
    
    // Parse query string
    size_t buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1 && buf_len < sizeof(buf)) {
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            char param[128];
            
            // Get email
            if (httpd_query_key_value(buf, "email", param, sizeof(param)) == ESP_OK) {
                strncpy(email, param, sizeof(email) - 1);
            }
            
            // Get password
            if (httpd_query_key_value(buf, "pass", param, sizeof(param)) == ESP_OK) {
                strncpy(password, param, sizeof(password) - 1);
            }
            
            // Get server
            if (httpd_query_key_value(buf, "server", param, sizeof(param)) == ESP_OK) {
                use_eu_server = (param[0] == '1');
            }
        }
    }
    
    // Test connection
    librelinkup_init(use_eu_server);
    esp_err_t err = librelinkup_login(email, password);
    
    if (err == ESP_OK) {
        // Get patient ID
        char patient_id[64];
        err = librelinkup_get_patient_id(patient_id, sizeof(patient_id));
        
        if (err == ESP_OK) {
            // Return success with patient ID
            char response[512];
            snprintf(response, sizeof(response), 
                     "{\"success\":true,\"patients\":[{\"id\":\"%s\",\"name\":\"Patient 1\"}]}",
                     patient_id);
            httpd_resp_set_type(req, "application/json");
            httpd_resp_send(req, response, strlen(response));
            librelinkup_logout();
            return ESP_OK;
        }
    }
    
    // Return error
    const char* error_response = "{\"success\":false,\"error\":\"Login failed\"}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, error_response, strlen(error_response));
    librelinkup_logout();
    return ESP_OK;
}

// HTTP GET handler for WiFi scan
static esp_err_t scan_get_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "WiFi scan requested");
    
    // Ensure WiFi is in correct mode for scanning
    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);
    if (mode != WIFI_MODE_APSTA && mode != WIFI_MODE_STA) {
        ESP_LOGE(TAG, "WiFi not in scanning mode: %d", mode);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "[]", 2);
        return ESP_OK;
    }
    
    // Start scan
    wifi_scan_config_t scan_config = {
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 100,
        .scan_time.active.max = 300
    };
    
    esp_err_t err = esp_wifi_scan_start(&scan_config, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WiFi scan failed: %s (0x%x)", esp_err_to_name(err), err);
        httpd_resp_send(req, "[]", 2);
        return ESP_OK;
    }
    
    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    ESP_LOGI(TAG, "Found %d access points", ap_count);
    
    if (ap_count > 0) {
        wifi_ap_record_t *ap_list = (wifi_ap_record_t *)malloc(sizeof(wifi_ap_record_t) * ap_count);
        if (ap_list == NULL) {
            httpd_resp_send(req, "[]", 2);
            return ESP_OK;
        }
        
        esp_wifi_scan_get_ap_records(&ap_count, ap_list);
        
        // Build JSON response
        char *json = (char *)malloc(4096);
        if (json == NULL) {
            free(ap_list);
            httpd_resp_send(req, "[]", 2);
            return ESP_OK;
        }
        
        int offset = 0;
        offset += snprintf(json + offset, 4096 - offset, "[");
        
        for (int i = 0; i < ap_count && i < 30 && offset < 3900; i++) {
            if (i > 0) offset += snprintf(json + offset, 4096 - offset, ",");
            // Escape quotes in SSID
            offset += snprintf(json + offset, 4096 - offset, "\"%s\"", (char*)ap_list[i].ssid);
        }
        offset += snprintf(json + offset, 4096 - offset, "]");
        
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, json, strlen(json));
        
        ESP_LOGI(TAG, "Sent scan results: %d networks", ap_count);
        
        free(json);
        free(ap_list);
    } else {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "[]", 2);
    }
    
    return ESP_OK;
}

// HTTP GET handler for settings page
static esp_err_t settings_get_handler(httpd_req_t *req) {
    httpd_resp_send(req, html_settings, strlen(html_settings));
    return ESP_OK;
}

// HTTP GET handler for loading settings
static esp_err_t settings_load_get_handler(httpd_req_t *req) {
    global_settings_t settings;
    esp_err_t err = global_settings_load(&settings);
    
    char response[256];
    if (err == ESP_OK) {
        snprintf(response, sizeof(response), 
                 "{\"success\":true,\"interval\":%lu,\"moon_lamp\":%s}",
                 settings.librelink_interval_minutes,
                 settings.moon_lamp_enabled ? "true" : "false");
    } else {
        snprintf(response, sizeof(response), 
                 "{\"success\":false,\"error\":\"Failed to load settings\"}");
    }
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, strlen(response));
    return ESP_OK;
}

// HTTP POST handler for saving settings
static esp_err_t settings_save_post_handler(httpd_req_t *req) {
    char buf[256];
    int ret, remaining = req->content_len;
    
    if (remaining >= sizeof(buf)) {
        const char* error_page = "<html><body><h1>Request too large</h1></body></html>";
        httpd_resp_send(req, error_page, strlen(error_page));
        return ESP_OK;
    }
    
    ret = httpd_req_recv(req, buf, remaining);
    if (ret <= 0) {
        return ESP_FAIL;
    }
    buf[ret] = '\0';
    
    global_settings_t settings;
    settings.librelink_interval_minutes = DEFAULT_LIBRELINK_INTERVAL_MINUTES;
    settings.moon_lamp_enabled = false;  // Default to off unless checked
    
    // Parse interval
    char *interval_start = strstr(buf, "interval=");
    if (interval_start) {
        interval_start += 9;
        char *interval_end = strchr(interval_start, '&');
        if (!interval_end) {
            interval_end = interval_start + strlen(interval_start);
        }
        
        char interval_str[16] = {0};
        int len = interval_end - interval_start;
        if (len < sizeof(interval_str)) {
            strncpy(interval_str, interval_start, len);
            settings.librelink_interval_minutes = atoi(interval_str);
            if (settings.librelink_interval_minutes < 1) {
                settings.librelink_interval_minutes = 1;
            }
        }
    }
    
    // Parse moon lamp checkbox (only present if checked)
    if (strstr(buf, "moon_lamp=1")) {
        settings.moon_lamp_enabled = true;
    }
    
    // Save settings
    esp_err_t err = global_settings_save(&settings);
    
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Global settings saved: interval=%lu min, moon_lamp=%s", 
                 settings.librelink_interval_minutes,
                 settings.moon_lamp_enabled ? "enabled" : "disabled");
        httpd_resp_send(req, success_page, strlen(success_page));
    } else {
        const char* error_page = "<html><body><h1>Error saving settings</h1></body></html>";
        httpd_resp_send(req, error_page, strlen(error_page));
    }
    
    return ESP_OK;
}

// OTA check handler
static esp_err_t ota_check_handler(httpd_req_t *req) {
    char new_version[32] = {0};
    esp_err_t ret = ota_check_for_update(new_version, sizeof(new_version));
    
    httpd_resp_set_type(req, "application/json");
    
    if (ret == ESP_OK) {
        // Update available
        char response[128];
        snprintf(response, sizeof(response), 
                "{\"updateAvailable\":true,\"currentVersion\":\"%s\",\"newVersion\":\"%s\"}", 
                ota_get_current_version(), new_version);
        httpd_resp_send(req, response, strlen(response));
    } else if (ret == ESP_ERR_NOT_FOUND) {
        // Already latest
        char response[128];
        snprintf(response, sizeof(response), 
                "{\"updateAvailable\":false,\"currentVersion\":\"%s\"}", 
                ota_get_current_version());
        httpd_resp_send(req, response, strlen(response));
    } else {
        // Error checking
        const char *error_response = "{\"error\":\"Failed to check for updates\"}";
        httpd_resp_send(req, error_response, strlen(error_response));
    }
    
    return ESP_OK;
}

// Forward declaration
static void ota_update_task(void *pvParameters);

// OTA update handler (triggers the update process)
static esp_err_t ota_update_handler(httpd_req_t *req) {
    if (!ota_is_safe_to_update()) {
        httpd_resp_set_type(req, "application/json");
        const char *error_response = "{\"error\":\"Not safe to update - check WiFi and power\"}";
        httpd_resp_send(req, error_response, strlen(error_response));
        return ESP_OK;
    }
    
    // Respond immediately before starting update
    httpd_resp_set_type(req, "application/json");
    const char *response = "{\"status\":\"Update started\"}";
    httpd_resp_send(req, response, strlen(response));
    
    // Start OTA update in a separate task (so HTTP response completes)
    xTaskCreate(ota_update_task, "ota_task", 8192, NULL, 5, NULL);
    
    return ESP_OK;
}

// Task function for OTA update
static void ota_update_task(void *pvParameters) {
    vTaskDelay(pdMS_TO_TICKS(1000));  // Give time for HTTP response
    ota_perform_update(NULL);  // Progress callback handled by main.c via display
    vTaskDelete(NULL);
}

// Captive portal redirect handler - serve portal page directly
static esp_err_t redirect_handler(httpd_req_t *req) {
    // Apple devices need specific headers to trigger captive portal
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_status(req, "200 OK");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_set_hdr(req, "Expires", "0");
    httpd_resp_send(req, html_main, strlen(html_main));
    return ESP_OK;
}

// Start HTTP server for provisioning
static void start_webserver(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_uri_handlers = 28;  // Increased for comprehensive captive portal coverage + new pages
    
    if (httpd_start(&server, &config) == ESP_OK) {
        // Main portal page
        httpd_uri_t root = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = root_get_handler
        };
        httpd_register_uri_handler(server, &root);
        
        // WiFi setup page
        httpd_uri_t wifi_page = {
            .uri = "/wifi",
            .method = HTTP_GET,
            .handler = wifi_get_handler
        };
        httpd_register_uri_handler(server, &wifi_page);
        
        // LibreLink setup page
        httpd_uri_t librelink_page = {
            .uri = "/librelink",
            .method = HTTP_GET,
            .handler = librelink_get_handler
        };
        httpd_register_uri_handler(server, &librelink_page);
        
        // Save credentials
        httpd_uri_t save = {
            .uri = "/save",
            .method = HTTP_POST,
            .handler = save_post_handler
        };
        httpd_register_uri_handler(server, &save);
        
        // OTA update endpoints
        httpd_uri_t ota_check = {
            .uri = "/ota/check",
            .method = HTTP_GET,
            .handler = ota_check_handler
        };
        httpd_register_uri_handler(server, &ota_check);
        
        httpd_uri_t ota_update = {
            .uri = "/ota/update",
            .method = HTTP_POST,
            .handler = ota_update_handler
        };
        httpd_register_uri_handler(server, &ota_update);
        
        // pd_register_uri_handler(server, &save);
        
        // WiFi scan endpoint
        httpd_uri_t scan = {
            .uri = "/scan",
            .method = HTTP_GET,
            .handler = scan_get_handler
        };
        httpd_register_uri_handler(server, &scan);
        
        // LibreLink endpoints
        httpd_uri_t libre_save = {
            .uri = "/libre/save",
            .method = HTTP_POST,
            .handler = libre_save_post_handler
        };
        httpd_register_uri_handler(server, &libre_save);
        
        httpd_uri_t libre_patients = {
            .uri = "/libre/patients",
            .method = HTTP_GET,
            .handler = libre_patients_get_handler
        };
        httpd_register_uri_handler(server, &libre_patients);
        
        httpd_uri_t libre_test = {
            .uri = "/libre/test",
            .method = HTTP_GET,
            .handler = libre_test_get_handler
        };
        httpd_register_uri_handler(server, &libre_test);
        
        // Global Settings endpoints
        httpd_uri_t settings_page = {
            .uri = "/settings",
            .method = HTTP_GET,
            .handler = settings_get_handler
        };
        httpd_register_uri_handler(server, &settings_page);
        
        httpd_uri_t settings_load = {
            .uri = "/settings/load",
            .method = HTTP_GET,
            .handler = settings_load_get_handler
        };
        httpd_register_uri_handler(server, &settings_load);
        
        httpd_uri_t settings_save = {
            .uri = "/settings/save",
            .method = HTTP_POST,
            .handler = settings_save_post_handler
        };
        httpd_register_uri_handler(server, &settings_save);
        
        // Captive portal detection URLs - serve portal page directly
        // Android
        httpd_uri_t generate_204 = {.uri = "/generate_204", .method = HTTP_GET, .handler = redirect_handler};
        httpd_uri_t gen_204 = {.uri = "/gen_204", .method = HTTP_GET, .handler = redirect_handler};
        
        // Apple iOS/macOS
        httpd_uri_t hotspot = {.uri = "/hotspot-detect.html", .method = HTTP_GET, .handler = redirect_handler};
        httpd_uri_t library = {.uri = "/library/test/success.html", .method = HTTP_GET, .handler = redirect_handler};
        httpd_uri_t apple_success = {.uri = "/success.html", .method = HTTP_GET, .handler = redirect_handler};
        httpd_uri_t apple_bag = {.uri = "/bag", .method = HTTP_GET, .handler = redirect_handler};
        
        // Windows
        httpd_uri_t ncsi = {.uri = "/ncsi.txt", .method = HTTP_GET, .handler = redirect_handler};
        httpd_uri_t connecttest = {.uri = "/connecttest.txt", .method = HTTP_GET, .handler = redirect_handler};
        
        // Generic
        httpd_uri_t success = {.uri = "/success.txt", .method = HTTP_GET, .handler = redirect_handler};
        httpd_uri_t redirect = {.uri = "/redirect", .method = HTTP_GET, .handler = redirect_handler};
        
        // Brave Browser
        httpd_uri_t brave = {.uri = "/detectportal", .method = HTTP_GET, .handler = redirect_handler};
        
        // Firefox
        httpd_uri_t firefox = {.uri = "/canonical.html", .method = HTTP_GET, .handler = redirect_handler};
        
        // Cloudflare WARP
        httpd_uri_t cloudflare1 = {.uri = "/cloudflareportal", .method = HTTP_GET, .handler = redirect_handler};
        httpd_uri_t cloudflare2 = {.uri = "/cloudflarecp", .method = HTTP_GET, .handler = redirect_handler};
        httpd_uri_t cloudflare3 = {.uri = "/connectivity-check", .method = HTTP_GET, .handler = redirect_handler};
        
        httpd_register_uri_handler(server, &generate_204);
        httpd_register_uri_handler(server, &gen_204);
        httpd_register_uri_handler(server, &hotspot);
        httpd_register_uri_handler(server, &library);
        httpd_register_uri_handler(server, &apple_success);
        httpd_register_uri_handler(server, &apple_bag);
        httpd_register_uri_handler(server, &success);
        httpd_register_uri_handler(server, &ncsi);
        httpd_register_uri_handler(server, &redirect);
        httpd_register_uri_handler(server, &connecttest);
        httpd_register_uri_handler(server, &brave);
        httpd_register_uri_handler(server, &firefox);
        httpd_register_uri_handler(server, &cloudflare1);
        httpd_register_uri_handler(server, &cloudflare2);
        httpd_register_uri_handler(server, &cloudflare3);
        
        ESP_LOGI(TAG, "Web server started with captive portal on port 80");
    }
}

// Polling timer for IP check (workaround for missing IP event)
static esp_timer_handle_t ip_poll_timer = NULL;

static void ip_poll_timer_callback(void* arg) {
    if (!wifi_connected && sta_netif) {
        esp_netif_ip_info_t ip_info;
        esp_err_t err = esp_netif_get_ip_info(sta_netif, &ip_info);
        
        if (err == ESP_OK && ip_info.ip.addr != 0) {
            ESP_LOGI(TAG, "IP detected via polling: " IPSTR, IP2STR(&ip_info.ip));
            
            // Stop the polling timer
            if (ip_poll_timer) {
                esp_timer_stop(ip_poll_timer);
            }
            
            // Manually trigger connection logic
            snprintf(current_ip, sizeof(current_ip), IPSTR, IP2STR(&ip_info.ip));
            wifi_connected = true;
            retry_count = 0;
            
            if (server == NULL) {
                ESP_LOGI(TAG, "Starting web server on STA IP: %s", current_ip);
                start_webserver();
            }
            
            if (connected_callback) {
                connected_callback();
            }
        }
    }
}

// WiFi event handler
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        ESP_LOGI(TAG, "WiFi connected, waiting for IP address from DHCP...");
        
        // Diagnose netif state
        if (sta_netif) {
            // Check if netif is up
            if (!esp_netif_is_netif_up(sta_netif)) {
                ESP_LOGW(TAG, "WARNING: sta_netif is NOT up! This may prevent DHCP from working.");
            }
            
            // Check DHCP status
            esp_netif_dhcp_status_t status;
            esp_err_t dhcp_err = esp_netif_dhcpc_get_status(sta_netif, &status);
            ESP_LOGI(TAG, "DHCP status query: err=%s, status=%d", esp_err_to_name(dhcp_err), status);
            
            // Force DHCP to restart - stop and start it
            if (dhcp_err == ESP_OK) {
                if (status != ESP_NETIF_DHCP_STOPPED) {
                    ESP_LOGI(TAG, "Stopping DHCP client to restart it...");
                    esp_netif_dhcpc_stop(sta_netif);
                    vTaskDelay(pdMS_TO_TICKS(100)); // Small delay
                }
                ESP_LOGI(TAG, "Starting DHCP client...");
                esp_err_t start_err = esp_netif_dhcpc_start(sta_netif);
                ESP_LOGI(TAG, "DHCP start result: %s", esp_err_to_name(start_err));
            }
        } else {
            ESP_LOGE(TAG, "ERROR: sta_netif is NULL!");
        }
        
        // Query current IP to see if DHCP already assigned it
        esp_netif_ip_info_t ip_info;
        if (sta_netif && esp_netif_get_ip_info(sta_netif, &ip_info) == ESP_OK) {
            if (ip_info.ip.addr != 0) {
                ESP_LOGI(TAG, "IP already assigned: " IPSTR, IP2STR(&ip_info.ip));
                // Manually trigger the got_ip logic since event didn't fire
                snprintf(current_ip, sizeof(current_ip), IPSTR, IP2STR(&ip_info.ip));
                wifi_connected = true;
                retry_count = 0;
                if (server == NULL) {
                    start_webserver();
                }
                if (connected_callback) {
                    connected_callback();
                }
            } else {
                ESP_LOGI(TAG, "Waiting for DHCP to assign IP...");
                
                // Start polling timer to check for IP every 500ms (workaround for missing event)
                if (ip_poll_timer == NULL) {
                    esp_timer_create_args_t timer_args = {
                        .callback = ip_poll_timer_callback,
                        .name = "ip_poll"
                    };
                    esp_err_t timer_create_err = esp_timer_create(&timer_args, &ip_poll_timer);
                    if (timer_create_err == ESP_OK) {
                        ESP_LOGI(TAG, "Created IP polling timer");
                    } else {
                        ESP_LOGE(TAG, "Failed to create timer: %s", esp_err_to_name(timer_create_err));
                    }
                }
                if (ip_poll_timer != NULL) {
                    esp_err_t timer_start_err = esp_timer_start_periodic(ip_poll_timer, 500000); // 500ms
                    if (timer_start_err == ESP_OK) {
                        ESP_LOGI(TAG, "Started IP polling timer (500ms interval)");
                    } else {
                        ESP_LOGE(TAG, "Failed to start timer: %s", esp_err_to_name(timer_start_err));
                    }
                }
            }
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t* disconnected = (wifi_event_sta_disconnected_t*) event_data;
        wifi_connected = false;
        strcpy(current_ip, "");
        
        // Stop polling timer if it's running
        if (ip_poll_timer != NULL) {
            esp_timer_stop(ip_poll_timer);
        }
        
        retry_count++;
        ESP_LOGW(TAG, "Disconnected from WiFi (reason: %d), retry %d/%d", disconnected->reason, retry_count, max_retry_attempts);
        
        if (retry_count >= max_retry_attempts) {
            // Max retries reached, notify failure
            ESP_LOGE(TAG, "WiFi connection failed after %d attempts", max_retry_attempts);
            retry_count = 0;
            if (failed_callback) {
                failed_callback();
            }
        } else {
            // Still have retries left
            if (disconnected_callback) {
                disconnected_callback();
            }
            esp_wifi_connect();
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        snprintf(current_ip, sizeof(current_ip), IPSTR, IP2STR(&event->ip_info.ip));
        wifi_connected = true;
        retry_count = 0;  // Reset retry counter on successful connection
        ESP_LOGI(TAG, "Connected! IP: %s", current_ip);
        
        // Start web server if not already running
        if (server == NULL) {
            ESP_LOGI(TAG, "Starting web server on STA IP: %s", current_ip);
            start_webserver();
        }
        
        if (connected_callback) {
            connected_callback();
        }
    }
}

// Start WiFi in AP mode
static esp_err_t start_ap_mode(void) {
    ESP_LOGI(TAG, "Starting AP mode: %s", AP_SSID);
    
    // If WiFi is running, stop and deinitialize it
    if (wifi_initialized) {
        ESP_LOGI(TAG, "Stopping current WiFi mode...");
        esp_wifi_stop();
        esp_wifi_deinit();
        wifi_initialized = false;
        
        // Destroy existing netifs
        if (sta_netif != NULL) {
            esp_netif_destroy(sta_netif);
            sta_netif = NULL;
        }
        if (ap_netif != NULL) {
            esp_netif_destroy(ap_netif);
            ap_netif = NULL;
        }
    }
    
    // Create both netifs BEFORE initializing WiFi for APSTA mode
    ap_netif = esp_netif_create_default_wifi_ap();
    sta_netif = esp_netif_create_default_wifi_sta();
    
    // Configure DNS for captive portal
    esp_netif_dns_info_t dns_info;
    dns_info.ip.u_addr.ip4.addr = ESP_IP4TOADDR(192, 168, 4, 1);
    dns_info.ip.type = ESP_IPADDR_TYPE_V4;
    esp_netif_set_dns_info(ap_netif, ESP_NETIF_DNS_MAIN, &dns_info);
    
    // Initialize WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    wifi_initialized = true;
    
    // Set mode to APSTA immediately after init
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_LOGI(TAG, "WiFi mode set to APSTA");
    
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = AP_SSID,
            .ssid_len = strlen(AP_SSID),
            .password = AP_PASS,
            .max_connection = AP_MAX_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };
    
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    // Verify mode was set correctly
    wifi_mode_t actual_mode;
    esp_wifi_get_mode(&actual_mode);
    ESP_LOGI(TAG, "WiFi mode set to: %d (should be 3 for APSTA)", actual_mode);
    
    start_webserver();
    
    ESP_LOGI(TAG, "AP mode started. Connect to '%s' and go to http://192.168.4.1", AP_SSID);
    return ESP_OK;
}

esp_err_t wifi_manager_init(void) {
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Initialize TCP/IP stack
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // Try to load saved credentials
    nvs_handle_t nvs_handle;
    char ssid[33] = {0};
    char password[65] = {0};
    size_t ssid_len = sizeof(ssid);
    size_t pass_len = sizeof(password);
    
    bool credentials_found = false;
    if (nvs_open(WIFI_NAMESPACE, NVS_READONLY, &nvs_handle) == ESP_OK) {
        if (nvs_get_str(nvs_handle, WIFI_SSID_KEY, ssid, &ssid_len) == ESP_OK &&
            nvs_get_str(nvs_handle, WIFI_PASS_KEY, password, &pass_len) == ESP_OK) {
            credentials_found = true;
        }
        nvs_close(nvs_handle);
    }
    
    if (credentials_found && strlen(ssid) > 0) {
        // Try STA mode first
        ESP_LOGI(TAG, "WiFi credentials found, attempting connection to: %s", ssid);
        
        // Create STA netif FIRST (standard order)
        sta_netif = esp_netif_create_default_wifi_sta();
        ESP_LOGI(TAG, "Created STA netif: %p", sta_netif);
        
        // Initialize WiFi
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
        wifi_initialized = true;
        ESP_LOGI(TAG, "WiFi subsystem initialized");
        
        // Register event handlers BEFORE setting config/starting
        ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
        ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));
        ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_LOST_IP, &wifi_event_handler, NULL));
        ESP_LOGI(TAG, "Event handlers registered");
        
        // Configure STA
        wifi_config_t sta_config = {0};
        strncpy((char*)sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid));
        strncpy((char*)sta_config.sta.password, password, sizeof(sta_config.sta.password));
        sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK; // Accept WPA2 or better (including WPA3)
        strncpy(current_ssid, ssid, sizeof(current_ssid) - 1);
        
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
        ESP_ERROR_CHECK(esp_wifi_start());
        
        return ESP_OK;
    } else {
        // No credentials - start AP mode with STA capability for scanning
        ESP_LOGI(TAG, "No WiFi credentials found, starting AP mode");
        
        // Create both netifs for APSTA mode
        ap_netif = esp_netif_create_default_wifi_ap();
        sta_netif = esp_netif_create_default_wifi_sta();
        
        // Configure DNS for captive portal
        esp_netif_dns_info_t dns_info;
        dns_info.ip.u_addr.ip4.addr = ESP_IP4TOADDR(192, 168, 4, 1);
        dns_info.ip.type = ESP_IPADDR_TYPE_V4;
        esp_netif_set_dns_info(ap_netif, ESP_NETIF_DNS_MAIN, &dns_info);
        
        // Initialize WiFi
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
        wifi_initialized = true;
        
        // Set mode to APSTA for scanning capability
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
        ESP_LOGI(TAG, "WiFi mode set to APSTA");
        
        // Configure AP
        wifi_config_t ap_config = {
            .ap = {
                .ssid = AP_SSID,
                .ssid_len = strlen(AP_SSID),
                .password = AP_PASS,
                .max_connection = AP_MAX_CONN,
                .authmode = WIFI_AUTH_WPA_WPA2_PSK
            },
        };
        
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
        ESP_ERROR_CHECK(esp_wifi_start());
        
        // Verify mode
        wifi_mode_t actual_mode;
        esp_wifi_get_mode(&actual_mode);
        ESP_LOGI(TAG, "WiFi mode verified: %d (3=APSTA)", actual_mode);
        
        // Start web server
        start_webserver();
        
        ESP_LOGI(TAG, "AP mode started. Connect to '%s' at http://192.168.4.1", AP_SSID);
        
        return ESP_OK;
    }
}

bool wifi_manager_is_connected(void) {
    return wifi_connected;
}

const char* wifi_manager_get_ssid(void) {
    return current_ssid;
}

const char* wifi_manager_get_ip(void) {
    return current_ip;
}

void wifi_manager_register_connected_cb(wifi_connected_cb_t cb) {
    connected_callback = cb;
}

void wifi_manager_register_disconnected_cb(wifi_disconnected_cb_t cb) {
    disconnected_callback = cb;
}

void wifi_manager_register_failed_cb(wifi_failed_cb_t cb) {
    failed_callback = cb;
}

esp_err_t wifi_manager_clear_credentials(void) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(WIFI_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }
    
    nvs_erase_key(nvs_handle, WIFI_SSID_KEY);
    nvs_erase_key(nvs_handle, WIFI_PASS_KEY);
    nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    
    ESP_LOGI(TAG, "WiFi credentials cleared");
    return ESP_OK;
}

bool wifi_manager_is_provisioned(void) {
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(WIFI_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        return false;
    }
    
    size_t ssid_len = 0;
    err = nvs_get_str(nvs, WIFI_SSID_KEY, NULL, &ssid_len);
    nvs_close(nvs);
    
    return (err == ESP_OK && ssid_len > 0);
}

esp_err_t wifi_manager_start_ap_mode(void) {
    return start_ap_mode();
}
