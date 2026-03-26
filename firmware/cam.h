/**
 * @file cam.h
 * @brief Camera subsystem interface.
 */

#pragma once
#include "esp_err.h"

/** Initialize the OV2640/OV5640 camera with PSRAM-backed frame buffers. */
esp_err_t init_camera(void);

/** FreeRTOS task: continuous JPEG capture into the shared frame queue. */
void camera_task(void *arg);

/** FreeRTOS task: save timestamped JPEGs to SD card (optional). */
void photo_task(void *arg);

/** Mount the SD card over SPI and create the output directory. */
esp_err_t mount_sdcard(void);
