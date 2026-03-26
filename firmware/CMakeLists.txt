# firmware/CMakeLists.txt — ESP-IDF component build file
set(SRCS
    main.c
    cam.c
    AP.c
)

idf_component_register(
    SRCS          ${SRCS}
    INCLUDE_DIRS  .
    PRIV_REQUIRES fatfs sdmmc esp32-camera esp_psram nvs_flash
                  esp_event esp_netif esp_wifi lwip
)
