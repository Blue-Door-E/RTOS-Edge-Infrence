/**
 * @file main.c
 * @brief Entry point for the ESP32-S3 Sense camera streaming firmware.
 *
 * Initializes the camera subsystem and spawns two FreeRTOS tasks pinned
 * to separate cores:
 *   - Core 1: camera_task  – captures JPEG frames and queues them
 *   - Core 0: ap_task      – runs a Wi-Fi AP and streams frames over TCP
 */

#include <stdio.h>
#include <time.h>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_psram.h"
#include "esp_camera.h"

#include "macros.h"
#include "cam.h"
#include "AP.h"

/* Shared queue: camera_task produces frames, ap_task consumes them. */
QueueHandle_t s_frame_q = NULL;

void app_main(void)
{
    /* Create the inter-task frame queue (depth 3). */
    s_frame_q = xQueueCreate(3, sizeof(camera_fb_t *));
    configASSERT(s_frame_q);

    ESP_LOGI(TAG, "XIAO ESP32-S3 Sense — real-time JPEG streaming");

    /* 1. Camera init (uses PSRAM for frame buffers). */
    ESP_ERROR_CHECK(init_camera());

    sensor_t *s = esp_camera_sensor_get();
    s->set_exposure_ctrl(s, 1);

    /* 2. Bring up the Wi-Fi access point. */
    wifi_ap_start();

    ESP_LOGI(TAG, "Camera task on core %d", xPortGetCoreID());
    vTaskDelay(pdMS_TO_TICKS(500));

    /* 3. Warm-up: discard a few initial frames while the sensor PLL locks. */
    for (int i = 0; i < 5; ++i) {
        camera_fb_t *w = esp_camera_fb_get();
        if (w) esp_camera_fb_return(w);
        vTaskDelay(pdMS_TO_TICKS(60));
    }

    /* 4. Launch the real-time tasks on dedicated cores. */
    xTaskCreatePinnedToCore(camera_task, "camera_task",
                            8192, NULL, tskIDLE_PRIORITY + 5, NULL, 1);

    xTaskCreatePinnedToCore(ap_task, "ap_task",
                            6144, NULL, tskIDLE_PRIORITY + 4, NULL, 0);
}
