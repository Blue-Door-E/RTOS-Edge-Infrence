#!/usr/bin/env python3
"""
main.py — Jetson inference orchestrator.

Connects to dual smart-glasses over BLE, receives JPEG frames from the
ESP32 camera over TCP, runs real-time PyTorch inference, and pushes
status text back to the glasses display.
"""

import asyncio
import os
import threading
import time
from dataclasses import dataclass

from BLE import DualBLE
from Display import compose_text, stream_loop
from ML import OptionalModel
from config import (
    LEFT_MAC,
    RIGHT_MAC,
    SAVE_EVERY_N_GLOBAL,
    SAVE_UNCERTAIN,
    UI_REFRESH_SEC,
)

try:
    import cv2
    CV2_OK = True
except Exception:
    CV2_OK = False


@dataclass
class LiveState:
    """Shared mutable state between the BLE loop and the stream thread."""
    connecting: bool = True
    connected: bool = False
    ip_status: str = ""
    prediction: str = "N/A"
    confidence: float = 0.0
    file_index: int = 0
    stop: bool = False
    attempt: int = 0
    last_infer_ms: float = 0.0
    last_frame_ms: float = 0.0
    last_ble_write_ms: float = 0.0
    last_pred_tperf: float = 0.0
    model_name: str = "NoModel"
    torch_device: str = "CPU"
    saving_on: bool = False
    last_saved_frame: int = -1
    last_saved_label: str = ""
    last_saved_conf: float = 0.0
    last_saved_kind: str = ""


STATE = LiveState()


async def main():
    # --- BLE connection ---
    ble = DualBLE(LEFT_MAC, RIGHT_MAC)
    print("[BLE] Connecting...")
    await ble.connect()
    print("[BLE] Connected.")

    await ble.prompt_0x11("Loading.")
    await ble.safe_text_0x4E("EDITH starting...\nPlease wait.\nPreparing subsystems...")
    await ble.prompt_0x11("Connecting")

    # --- Model loading ---
    model = OptionalModel()

    STATE.model_name = (
        os.path.basename(model.model_path) if getattr(model, "model_path", "") else "NoModel"
    )
    STATE.torch_device = getattr(model, "device_tag", "CPU")
    STATE.saving_on = bool(
        model.ready and CV2_OK and (SAVE_UNCERTAIN or (SAVE_EVERY_N_GLOBAL and SAVE_EVERY_N_GLOBAL > 0))
    )

    # --- Launch TCP stream thread ---
    t = threading.Thread(target=stream_loop, args=(STATE, model), daemon=True)
    t.start()

    try:
        last_txt = ""
        last_sent_t = 0.0
        last_sent_frame = -1

        while True:
            txt = compose_text(STATE)
            now = time.perf_counter()
            frame = STATE.file_index

            force_periodic = (now - last_sent_t) >= 1.0
            should_send = (txt != last_txt) or (frame != last_sent_frame) or force_periodic

            if should_send:
                t_ble0 = time.perf_counter()
                try:
                    await ble.safe_text_0x4E(txt)
                except Exception as e:
                    print("[BLE] Write failed:", repr(e))
                finally:
                    STATE.last_ble_write_ms = (time.perf_counter() - t_ble0) * 1000.0

                last_txt = txt
                last_sent_t = now
                last_sent_frame = frame

            await asyncio.sleep(UI_REFRESH_SEC)

    except KeyboardInterrupt:
        pass
    finally:
        STATE.stop = True
        try:
            await ble.text_0x4E("Shutting down...\nGoodbye.")
        except Exception:
            pass
        await ble.disconnect()
        print("[BLE] Disconnected.")


if __name__ == "__main__":
    asyncio.run(main())
