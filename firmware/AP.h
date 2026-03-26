/**
 * @file AP.h
 * @brief Wi-Fi Access Point and TCP streaming interface.
 */

#pragma once

/** Bring up the ESP32 soft-AP (call once from app_main). */
void wifi_ap_start(void);

/** FreeRTOS task: TCP server that streams JPEG frames to clients. */
void ap_task(void *arg);
