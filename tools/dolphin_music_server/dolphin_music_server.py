#!/usr/bin/env python3
"""
DolphinMusic companion server - captures audio from a chosen input device
on your computer and serves it in small chunks over HTTP for the Flipper's
DolphinMusic app (via the WiFi Devboard/FlipperHTTP) to continuously pull
and play through its speaker - a lo-fi wireless-speaker setup.

To cast your computer's SYSTEM audio (not a microphone), you need a
loopback/virtual audio device first - this server just reads from whatever
input device you point it at, it can't create that routing for you:
  macOS:   install BlackHole (https://existential.audio/blackhole/), then
           route your system output through it (e.g. a Multi-Output Device
           in Audio MIDI Setup so you still hear it live too).
  Windows: enable "Stereo Mix" in Sound settings, or use a virtual audio
           cable tool (e.g. VB-CABLE).
  Linux:   use the PulseAudio/PipeWire "monitor" source of your output
           device - usually appears automatically in the device list.

Requires: pip install flask sounddevice numpy

Usage:
    python3 dolphin_music_server.py --list-devices
    python3 dolphin_music_server.py --device <id_or_name> [--port 8000]
"""
import argparse
import sys
import threading

import numpy as np
import sounddevice as sd
from flask import Flask, Response, jsonify

app = Flask(__name__)

SAMPLE_RATE = 8000
CHUNK_SECONDS = 2.0
CHUNK_SAMPLES = int(SAMPLE_RATE * CHUNK_SECONDS)
MAX_RING_BYTES = SAMPLE_RATE * 10  # cap backlog at 10s so latency can't grow unbounded

ring_lock = threading.Lock()
ring_buffer = bytearray()


def audio_callback(indata, frames, time_info, status):
    if status:
        print(status, file=sys.stderr)
    mono = indata.mean(axis=1) if indata.ndim > 1 else indata
    samples_u8 = np.clip((mono * 127.5) + 127.5, 0, 255).astype(np.uint8)
    with ring_lock:
        ring_buffer.extend(samples_u8.tobytes())
        if len(ring_buffer) > MAX_RING_BYTES:
            del ring_buffer[: len(ring_buffer) - MAX_RING_BYTES]


@app.route("/chunk")
def chunk():
    with ring_lock:
        take = min(len(ring_buffer), CHUNK_SAMPLES)
        data = bytes(ring_buffer[:take])
        del ring_buffer[:take]
    return Response(data, mimetype="application/octet-stream")


@app.route("/")
def index():
    return jsonify({"status": "ok", "sample_rate": SAMPLE_RATE})


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--list-devices", action="store_true")
    parser.add_argument("--device", type=str, default=None)
    parser.add_argument("--port", type=int, default=8000)
    args = parser.parse_args()

    if args.list_devices:
        print(sd.query_devices())
        return

    if args.device is None:
        print("Specify --device <id_or_name> (use --list-devices to see options)")
        sys.exit(1)

    device = int(args.device) if args.device.isdigit() else args.device
    stream = sd.InputStream(
        device=device,
        samplerate=SAMPLE_RATE,
        callback=audio_callback,
        blocksize=1024,
    )
    stream.start()
    print(f"Capturing from device {args.device} at {SAMPLE_RATE} Hz")
    print(f"DolphinMusic server listening on 0.0.0.0:{args.port}")
    print("Point the Flipper app's server address at this computer's LAN IP")
    print("(not localhost - the WiFi Devboard is a separate device)")
    app.run(host="0.0.0.0", port=args.port, threaded=True)


if __name__ == "__main__":
    main()
