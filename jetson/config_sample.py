"""
config_sample.py — Sample configuration file.

Copy this file to config.py and fill in your device-specific values.
All secrets can also be set via environment variables.
"""

import os

# BLE device MAC addresses (find with `bluetoothctl scan on`)
LEFT_MAC = os.getenv("GLASSES_LEFT_MAC", "xx:xx:xx:xx:xx:xx")
RIGHT_MAC = os.getenv("GLASSES_RIGHT_MAC", "xx:xx:xx:xx:xx:xx")

# Bleak address type — use "random" when bluetoothctl shows "(random)"
ADDRESS_TYPE = os.getenv("GLASSES_ADDRESS_TYPE", "random")

# Nordic UART Service UUIDs
NUS_RX = os.getenv("GLASSES_NUS_RX", "6e400002-b5a3-f393-e0a9-e50e24dcca9e")
NUS_TX = os.getenv("GLASSES_NUS_TX", "6e400003-b5a3-f393-e0a9-e50e24dcca9e")

# ESP32 Access Point network settings
WIFI_SSID = os.getenv("GLASSES_WIFI_SSID", "")
WIFI_PASSWORD = os.getenv("GLASSES_WIFI_PASSWORD", "")
HOST = os.getenv("GLASSES_HOST", "192.168.4.1")
PORT = int(os.getenv("GLASSES_PORT", "8080"))

# Inference settings
IMG_SIZE = 400
MODEL_DIR = "outputs/Hide"
BLE_CHUNK = 180
CONF_THRESHOLD = 0.70

# Frame-saving settings (for active-learning dataset building)
SAVE_DIR = "dataset/live_capture"
SAVE_COOLDOWN_SEC = 2.0
SAVE_CONFIDENT_EVERY_N = 20
SAVE_UNCERTAIN = True
SAVE_EVERY_N_GLOBAL = SAVE_CONFIDENT_EVERY_N
PRINT_EVERY_N = 30

# UI refresh rate (seconds)
UI_REFRESH_SEC = 0.3
