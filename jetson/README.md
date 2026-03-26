# Jetson — On-Device ML Inference Engine

Real-time gesture classification and smart-glasses display driver running on an **NVIDIA Jetson** (Orin Nano / Xavier NX).

## What It Does

This Python application is the brain of the AI glasses system. It receives a live JPEG stream from the ESP32 camera over TCP/Wi-Fi, runs a custom-trained ResNet-18 model for hand-gesture classification, and pushes real-time status text back to the glasses display over Bluetooth Low Energy.

### Pipeline

```
ESP32 Camera (Wi-Fi TCP)
        |
   [ JPEG decode ]
        |
   [ PyTorch inference — ResNet-18 on CUDA ]
        |
   [ BLE text display → Smart-glasses ]
        |
   [ Optional frame saving → dataset/ ]
```

All inference happens locally on the Jetson GPU with zero cloud dependency. Typical end-to-end latency from frame capture to prediction display is under 100ms.

## File Overview

| File | Description |
|------|-------------|
| `main.py` | Entry point — async BLE loop + spawns the TCP stream thread |
| `BLE.py` | Dual BLE client for the smart-glasses (Nordic UART Service) |
| `ML.py` | TorchScript model loader with CUDA/CPU auto-detection |
| `Display.py` | TCP frame receiver, inference runner, and UI text composer |
| `config_sample.py` | Sample configuration — copy to `config.py` and fill in your values |

## Setup

### 1. Install Dependencies

```bash
pip install bleak opencv-python numpy
# PyTorch + torchvision — use NVIDIA's Jetson wheels:
# https://forums.developer.nvidia.com/t/pytorch-for-jetson/
```

### 2. Configure

```bash
cp config_sample.py config.py
# Edit config.py with your BLE MAC addresses and network settings
```

### 3. Add Your Model

Place your TorchScript model and labels file:

```
outputs/Hide/resnet18_scripted.pt
outputs/Hide/labels.txt
```

### 4. Run

```bash
python3 main.py
```

The system will connect to the glasses over BLE, start receiving frames from the ESP32, and display live predictions on the glasses.

## Key Features

- **Zero-latency inference**: All ML runs on the Jetson GPU — no cloud round-trip
- **Automatic BLE reconnection**: Handles dropped connections gracefully
- **Active learning**: Optionally saves low-confidence and periodic frames for retraining
- **MTU-aware chunking**: Automatically negotiates BLE packet sizes for reliable writes
