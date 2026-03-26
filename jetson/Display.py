"""
Display.py — TCP frame receiver, inference runner, and UI text composer.

Connects to the ESP32 camera's TCP stream, decodes JPEG frames, runs
ML inference, optionally saves frames for dataset building, and composes
the four-line status text displayed on the smart-glasses.
"""

from __future__ import annotations

import os
import socket
import struct
import time
from datetime import datetime

import numpy as np

from config import (
    CONF_THRESHOLD,
    HOST,
    PORT,
    PRINT_EVERY_N,
    SAVE_COOLDOWN_SEC,
    SAVE_DIR,
    SAVE_EVERY_N_GLOBAL,
    SAVE_UNCERTAIN,
)

try:
    import cv2
    CV2_OK = True
except Exception:
    CV2_OK = False


def _now_str() -> str:
    return datetime.now().strftime("%Y%m%d-%H%M%S")


def ensure_dir(path: str):
    os.makedirs(path, exist_ok=True)


def stream_loop(state, model):
    """
    Background thread: receive frames, run inference, save selectively.

    Connects to the ESP32 TCP server and processes frames in a loop.
    Saves low-confidence predictions to an 'uncertain' folder and
    periodically saves high-confidence frames to class-specific folders
    for active-learning dataset expansion.
    """
    ensure_dir(SAVE_DIR)
    uncertain_path = os.path.join(SAVE_DIR, "uncertain")
    ensure_dir(uncertain_path)

    class_dirs = {}
    last_saved_time = {"__uncertain__": 0.0, "__global__": 0.0}

    if model.ready:
        for lbl in model.labels:
            p = os.path.join(SAVE_DIR, lbl)
            ensure_dir(p)
            class_dirs[lbl] = p

    backoff = 1.0

    while not state.stop:
        state.attempt += 1
        state.connecting = True
        state.connected = False
        state.ip_status = f"Attempting {HOST}:{PORT} (try {state.attempt})"

        s = socket.socket()
        s.settimeout(4)

        try:
            s.connect((HOST, PORT))
            s.settimeout(None)

            state.connected = True
            state.connecting = False
            state.ip_status = f"Connected {HOST}:{PORT}"
            print("[Stream] Connected", HOST, PORT)
            backoff = 1.0
            count = state.file_index

            while not state.stop:
                t0 = time.perf_counter()

                # Read frame header (4-byte big-endian length).
                hdr = s.recv(4)
                if not hdr or len(hdr) < 4:
                    raise RuntimeError("EOF header")

                frame_len = struct.unpack(">I", hdr)[0]
                data = b""
                while len(data) < frame_len:
                    pkt = s.recv(frame_len - len(data))
                    if not pkt:
                        raise RuntimeError("EOF mid-frame")
                    data += pkt

                # Decode JPEG.
                img = None
                if CV2_OK:
                    img = cv2.imdecode(np.frombuffer(data, np.uint8), cv2.IMREAD_COLOR)

                t_infer0 = time.perf_counter()

                # Run inference.
                if img is not None and model.ready:
                    pred_label, conf, pred_idx = model.predict(img)
                    state.prediction = pred_label
                    state.confidence = conf
                else:
                    pred_label, conf, pred_idx = "N/A", 0.0, -1
                    state.prediction, state.confidence = "N/A", 0.0

                t_infer1 = time.perf_counter()
                state.last_infer_ms = (t_infer1 - t_infer0) * 1000.0
                state.last_frame_ms = (t_infer1 - t0) * 1000.0
                state.last_pred_tperf = t_infer1

                ts = time.time()
                ts_str = _now_str()

                # --- Frame saving for dataset building ---
                if model.ready and CV2_OK and img is not None and pred_label != "N/A":
                    # Low-confidence → uncertain folder.
                    if conf < CONF_THRESHOLD and SAVE_UNCERTAIN:
                        if ts - last_saved_time["__uncertain__"] >= SAVE_COOLDOWN_SEC:
                            fname = f"{ts_str}_pred-{pred_label}_conf-{conf:.3f}.jpg"
                            cv2.imwrite(os.path.join(uncertain_path, fname), img)
                            last_saved_time["__uncertain__"] = ts
                            state.last_saved_frame = count + 1
                            state.last_saved_label = pred_label
                            state.last_saved_conf = conf
                            state.last_saved_kind = "uncertain"

                    # Periodic saving to class folder.
                    if SAVE_EVERY_N_GLOBAL > 0:
                        next_count = count + 1
                        if (next_count % SAVE_EVERY_N_GLOBAL) == 0:
                            if ts - last_saved_time["__global__"] >= SAVE_COOLDOWN_SEC:
                                outdir = class_dirs.get(pred_label) or os.path.join(SAVE_DIR, pred_label)
                                ensure_dir(outdir)
                                fname = f"{ts_str}_{pred_label}_conf-{conf:.3f}_frame-{next_count}.jpg"
                                cv2.imwrite(os.path.join(outdir, fname), img)
                                last_saved_time["__global__"] = ts
                                state.last_saved_frame = next_count
                                state.last_saved_label = pred_label
                                state.last_saved_conf = conf
                                state.last_saved_kind = "global"

                count += 1
                state.file_index = count

                if PRINT_EVERY_N > 0 and (count % PRINT_EVERY_N) == 0:
                    print(
                        f"[Perf] frame#{count} "
                        f"net+decode+infer_ms={state.last_frame_ms:.1f} "
                        f"(infer={state.last_infer_ms:.1f})"
                    )

        except Exception as e:
            print("[Stream] Not connected:", repr(e))
            state.connected = False
            state.connecting = True
            state.ip_status = f"Attempting {HOST}:{PORT} (retry in {int(backoff)}s)"

            try:
                s.close()
            except Exception:
                pass

            sleep_s = min(10.0, backoff)
            for _ in range(int(sleep_s * 10)):
                if state.stop:
                    break
                time.sleep(0.1)
            backoff = min(10.0, backoff * 1.8)
            continue

        try:
            s.close()
        except Exception:
            pass


def compose_text(state) -> str:
    """Build the four-line status string displayed on the smart-glasses."""
    ip_part = f"{HOST}:{PORT}" if state.connected else (state.ip_status or f"{HOST}:{PORT}")
    row1 = f"{ip_part} | {state.model_name} | {state.torch_device}"

    row2 = (
        "Prediction: N/A"
        if state.prediction == "N/A"
        else f"Prediction: {state.prediction} ({state.confidence:.2f})"
    )

    if state.saving_on:
        if state.last_saved_frame >= 0:
            row3 = (
                f"Saving: ON | Last: {state.last_saved_kind} "
                f"#{state.last_saved_frame} {state.last_saved_label} "
                f"({state.last_saved_conf:.2f})"
            )
        else:
            row3 = "Saving: ON | Last: none"
    else:
        row3 = "Saving: OFF | Last: none"

    row4 = f"Frame #: {state.file_index}"

    return f"{row1}\n{row2}\n{row3}\n{row4}"
