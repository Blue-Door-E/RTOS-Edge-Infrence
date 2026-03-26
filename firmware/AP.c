/**
 * @file AP.c
 * @brief Wi-Fi Access Point and TCP frame-streaming server.
 *
 * Brings up a WPA2-PSK soft-AP on the ESP32-S3 and runs a TCP server
 * that streams JPEG frames from the shared queue to a connected client
 * (the Jetson inference host).
 *
 * Wire protocol (per frame):
 *   [4-byte big-endian length][JPEG payload]
 */

#include <string.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_camera.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "lwip/sockets.h"

#include "macros.h"
#include "configs.h"

/* Frame queue produced by camera_task (main.c). */
extern QueueHandle_t s_frame_q;

/* ---------------------------------------------------------------------------
 * Wi-Fi Access Point Setup
 * --------------------------------------------------------------------------- */

void wifi_ap_start(void)
{
    /* NVS — required by the Wi-Fi driver. */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(err);
    }

    /* Network interface and default event loop. */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_netif_create_default_wifi_ap() ? ESP_OK : ESP_FAIL);

    /* Wi-Fi driver. */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_country_t country = {
        .cc     = "US",
        .schan  = 1,
        .nchan  = 11,
        .policy = WIFI_COUNTRY_POLICY_MANUAL,
    };
    esp_wifi_set_country(&country);
    esp_wifi_set_ps(WIFI_PS_NONE);   /* Disable power-save for low latency. */

    /* AP configuration. */
    wifi_config_t ap_cfg = {
        .ap = {
            .ssid           = AP_SSID,
            .ssid_len       = 0,
            .channel        = AP_CHANNEL,
            .password       = AP_PASS,
            .max_connection = AP_MAX_CONN,
            .authmode       = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg        = { .required = true },
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
}

/* ---------------------------------------------------------------------------
 * TCP Streaming Task
 * --------------------------------------------------------------------------- */

/**
 * Accepts one TCP client at a time and streams queued JPEG frames.
 *
 * Each frame is sent as a 4-byte big-endian length prefix followed by the
 * raw JPEG bytes.  If the client disconnects, the task loops back and
 * waits for a new connection.
 */
void ap_task(void *arg)
{
    ESP_LOGI(TAG, "AP task on core %d", xPortGetCoreID());
    vTaskDelay(pdMS_TO_TICKS(200));

    ESP_LOGI(TAG, "SoftAP ready — connect to 192.168.4.1:%d", TCP_PORT);

    int srv = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in addr = { 0 };
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(TCP_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    int one = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    bind(srv, (struct sockaddr *)&addr, sizeof(addr));
    listen(srv, 1);

    for (;;) {
        ESP_LOGI(TAG, "Waiting for client...");
        int client = accept(srv, NULL, NULL);
        if (client < 0) continue;

        ESP_LOGI(TAG, "Client connected");

        while (1) {
            camera_fb_t *fb = NULL;
            if (xQueueReceive(s_frame_q, &fb, pdMS_TO_TICKS(2000)) == pdTRUE && fb) {
                uint32_t len_be = htonl(fb->len);

                if (send(client, &len_be, sizeof(len_be), 0) != sizeof(len_be)) {
                    esp_camera_fb_return(fb);
                    break;
                }
                if (send(client, fb->buf, fb->len, 0) != (int)fb->len) {
                    esp_camera_fb_return(fb);
                    break;
                }

                esp_camera_fb_return(fb);
            }
        }

        close(client);
    }
}
