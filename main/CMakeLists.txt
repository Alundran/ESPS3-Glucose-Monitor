idf_component_register(SRCS "global_settings.c" "ir_transmitter.c" "main.c" "display.c" "wifi_manager.c" "librelinkup.c" "libre_credentials.c" "ota_update.c"
                    INCLUDE_DIRS "."
                    EMBED_FILES "../supreme_glucose_splash.png" "../ahs_lala.wav" "../ahs_surprise.wav"
                    REQUIRES lvgl__lvgl nvs_flash esp_wifi esp_netif esp_http_server esp_http_client driver esp_timer json esp-tls app_update esp_https_ota espressif__esp-box-3 espressif__esp_codec_dev)
