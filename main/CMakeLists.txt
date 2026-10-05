idf_component_register(
    SRCS "main.c"
         "usage_parse.c"
         "usage_store.c"
         "usage_client.c"
         "wifi_mgr.c"
         "time_sync.c"
         "ui_home.c"
         "power_idle.c"
    INCLUDE_DIRS "."
    REQUIRES bsp driver esp_event esp_http_client esp_hw_support esp_netif
             esp_timer esp_wifi lwip mbedtls nvs_flash
)
