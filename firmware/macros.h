/**
 * @file macros.h
 * @brief Pin definitions for the XIAO ESP32-S3 Sense board.
 */

#pragma once

/* ---------------------------------------------------------------------------
 * SD Card (SPI mode on the Sense expansion board)
 * --------------------------------------------------------------------------- */
#define SD_CS_GPIO    21
#define SD_SCLK_GPIO   7
#define SD_MISO_GPIO   8
#define SD_MOSI_GPIO   9

/* ---------------------------------------------------------------------------
 * Camera (OV2640 / OV5640 via DVP + SCCB)
 * --------------------------------------------------------------------------- */
#define CAM_PWDN      -1   /* Not wired on Sense */
#define CAM_RESET     -1   /* Not wired on Sense */
#define CAM_XCLK      10
#define CAM_SIOD       40  /* SCCB SDA */
#define CAM_SIOC       39  /* SCCB SCL */
#define CAM_D7         48  /* Y9 */
#define CAM_D6         11  /* Y8 */
#define CAM_D5         12  /* Y7 */
#define CAM_D4         14  /* Y6 */
#define CAM_D3         16  /* Y5 */
#define CAM_D2         18  /* Y4 */
#define CAM_D1         17  /* Y3 */
#define CAM_D0         15  /* Y2 */
#define CAM_VSYNC      38
#define CAM_HREF       47
#define CAM_PCLK       13

/* ---------------------------------------------------------------------------
 * Network Defaults
 * --------------------------------------------------------------------------- */
#define AP_CHANNEL      6
#define AP_MAX_CONN     1
#define TCP_PORT     8080  /* Clients connect to 192.168.4.1:8080 */

/* Shared log tag. */
extern char *TAG;
