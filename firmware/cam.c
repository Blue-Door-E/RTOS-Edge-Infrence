/**
 * @file cam.c
 * @brief Camera driver for the OV2640/OV5640 on the XIAO ESP32-S3 Sense.
 *
 * Provides camera initialization, a continuous capture task that feeds
 * frames into a shared FreeRTOS queue, and optional SD-card photo saving.
 */

#include <stdio.h>
#include <stdbool.h>
#include <sys/time.h>
#include <sys/stat.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_camera.h"
#include "driver/gpio.h"
#include "driver/spi_common.h"
#include "driver/sdspi_host.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#include "macros.h"
#include "cam.h"

/* Shared frame queue (owned by main.c). */
extern QueueHandle_t s_frame_q;

char *TAG = "xiao_sense_cam";

/* ---------------------------------------------------------------------------
 * Helpers
 * --------------------------------------------------------------------------- */

/** Check whether a JPEG buffer ends with the standard EOI marker (0xFF 0xD9). */
static inline bool has_jpeg_eoi(const uint8_t *buf, size_t len)
{
    return len >= 2 && buf[len - 2] == 0xFF && buf[len - 1] == 0xD9;
}

/* ---------------------------------------------------------------------------
 * Camera Initialization
 * --------------------------------------------------------------------------- */

esp_err_t init_camera(void)
{
    camera_config_t config = {
        .pin_pwdn     = CAM_PWDN,
        .pin_reset    = CAM_RESET,
        .pin_xclk     = CAM_XCLK,
        .pin_sccb_sda = CAM_SIOD,
        .pin_sccb_scl = CAM_SIOC,
        .pin_d7       = CAM_D7, .pin_d6 = CAM_D6,
        .pin_d5       = CAM_D5, .pin_d4 = CAM_D4,
        .pin_d3       = CAM_D3, .pin_d2 = CAM_D2,
        .pin_d1       = CAM_D1, .pin_d0 = CAM_D0,
        .pin_vsync    = CAM_VSYNC,
        .pin_href     = CAM_HREF,
        .pin_pclk     = CAM_PCLK,

        .xclk_freq_hz = 10000000,
        .ledc_timer   = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,
        .pixel_format = PIXFORMAT_JPEG,
        .frame_size   = FRAMESIZE_VGA,
        .jpeg_quality = 20,
        .fb_count     = 2,
        .fb_location  = CAMERA_FB_IN_PSRAM,
        .grab_mode    = CAMERA_GRAB_LATEST,
    };

    if (!esp_psram_is_initialized()) {
        ESP_LOGE(TAG, "PSRAM not initialized");
        return ESP_FAIL;
    }

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK)
        ESP_LOGE(TAG, "Camera init failed: %s", esp_err_to_name(err));
    return err;
}

/* ---------------------------------------------------------------------------
 * FreeRTOS Tasks
 * --------------------------------------------------------------------------- */

/**
 * Continuous capture task (pinned to Core 1).
 *
 * Grabs JPEG frames from the camera sensor and pushes them into
 * s_frame_q.  If the queue is full the frame is silently dropped
 * so that capture cadence is never blocked by a slow consumer.
 */
void camera_task(void *arg)
{
    /* Brief settle time after boot. */
    vTaskDelay(pdMS_TO_TICKS(600));

    /* Discard a few warm-up frames. */
    for (int i = 0; i < 5; ++i) {
        camera_fb_t *w = esp_camera_fb_get();
        if (w) esp_camera_fb_return(w);
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    for (;;) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGE(TAG, "Capture failed");
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        /* Non-blocking send; drop frame if queue is full. */
        if (xQueueSend(s_frame_q, &fb, 0) != pdTRUE) {
            esp_camera_fb_return(fb);
        }

        vTaskDelay(pdMS_TO_TICKS(200));   /* ~5 fps */
    }
}

/**
 * (Optional) Photo-to-SD task — saves timestamped JPEGs to /sdcard/pics/.
 *
 * Currently unused in the streaming pipeline but retained for offline
 * data-collection workflows.
 */
void photo_task(void *arg)
{
    int n = 0;

    while (1) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb || !fb->buf || fb->len == 0) {
            if (fb) esp_camera_fb_return(fb);
            ESP_LOGW(TAG, "Empty frame dropped");
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        if (!has_jpeg_eoi(fb->buf, fb->len)) {
            esp_camera_fb_return(fb);
            ESP_LOGW(TAG, "Truncated JPEG dropped (len=%u)", (unsigned)fb->len);
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        struct timeval tv;
        gettimeofday(&tv, NULL);
        long long ms = (long long)tv.tv_sec * 1000LL + tv.tv_usec / 1000;

        char path[64];
        snprintf(path, sizeof(path), "/sdcard/pics/%lld.jpg", ms);

        FILE *f = fopen(path, "wb");
        if (!f) {
            ESP_LOGE(TAG, "Open failed: %s", path);
        } else {
            size_t wrote = fwrite(fb->buf, 1, fb->len, f);
            fclose(f);
            if (++n % 10 == 0)
                ESP_LOGI(TAG, "Saved %s (%u/%u bytes)",
                         path, (unsigned)wrote, (unsigned)fb->len);
        }

        esp_camera_fb_return(fb);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/* ---------------------------------------------------------------------------
 * SD Card Mount (optional — used when saving frames locally)
 * --------------------------------------------------------------------------- */

esp_err_t mount_sdcard(void)
{
    gpio_reset_pin(SD_CS_GPIO);
    gpio_set_direction(SD_CS_GPIO, GPIO_MODE_INPUT);

    spi_bus_config_t bus_cfg = {
        .mosi_io_num   = SD_MOSI_GPIO,
        .miso_io_num   = SD_MISO_GPIO,
        .sclk_io_num   = SD_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs  = SD_CS_GPIO;
    slot_config.host_id  = host.slot;

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files              = 5,
        .allocation_unit_size   = 16 * 1024,
    };

    sdmmc_card_t *card = NULL;
    esp_err_t err = esp_vfs_fat_sdspi_mount("/sdcard", &host,
                                             &slot_config, &mount_cfg, &card);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SD mount failed: %s", esp_err_to_name(err));
        return err;
    }

    sdmmc_card_print_info(stdout, card);
    mkdir("/sdcard/pics", 0775);
    return ESP_OK;
}
