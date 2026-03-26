# Firmware — ESP32-S3 Camera Streaming

Real-time JPEG capture and Wi-Fi streaming firmware for the **XIAO ESP32-S3 Sense**, built on **ESP-IDF** and **FreeRTOS**.

## What It Does

The firmware turns the ESP32-S3 Sense board into a wireless camera that streams JPEG frames over a local TCP connection. It is designed to run headless inside a portable enclosure (fanny pack) alongside an NVIDIA Jetson for on-device ML inference.

### Architecture

The system uses a dual-core FreeRTOS design to maximize throughput:

| Core | Task | Responsibility |
|------|------|----------------|
| 1 | `camera_task` | Captures JPEG frames from the OV2640 sensor via DVP and queues them into PSRAM-backed buffers |
| 0 | `ap_task` | Hosts a WPA2-PSK Wi-Fi access point and streams queued frames to a connected TCP client |

A **zero-copy pipeline** moves frames from camera → PSRAM → DMA → CPU → TCP socket with minimal latency.

### Wire Protocol

Each frame is sent over TCP as:

```
[4-byte big-endian length][raw JPEG payload]
```

The Jetson client connects to `192.168.4.1:8080` and reads this stream continuously.

## File Overview

| File | Description |
|------|-------------|
| `main.c` | Entry point — initializes hardware, spawns FreeRTOS tasks |
| `cam.c` | Camera driver — OV2640 init, continuous capture, JPEG validation |
| `cam.h` | Camera interface header |
| `AP.c` | Wi-Fi AP setup + TCP streaming server |
| `AP.h` | AP interface header |
| `macros.h` | XIAO ESP32-S3 Sense pin definitions |
| `configs.h` | Wi-Fi SSID/password (edit before flashing) |
| `CMakeLists.txt` | ESP-IDF build configuration |

## Building

Requires the [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/) toolchain (v5.x recommended) and the [esp32-camera](https://github.com/espressif/esp32-camera) component.

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

## Configuration

Edit `configs.h` to change the Wi-Fi AP credentials before flashing:

```c
#define AP_SSID  "AI Glasses"
#define AP_PASS  "mypassword"
```

Network parameters (channel, port, max connections) are defined in `macros.h`.
