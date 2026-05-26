import sys
import time
import argparse
from collections import deque

import numpy as np
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
import serial
import serial.tools.list_ports


def find_esp_ports():
    ports = list(serial.tools.list_ports.comports())
    esp = []
    for p in ports:
        if any(kw in p.description.lower() for kw in ("cp210", "ch340", "ft232", "silicon labs", "usb serial")):
            esp.append(p.device)
    return esp


def parse_args():
    parser = argparse.ArgumentParser(description="Real-time PPG raw data viewer")
    parser.add_argument("port", nargs="?", default=None, help="Serial port")
    parser.add_argument("--baud", type=int, default=921600)
    parser.add_argument("--fft", action="store_true", help="Show FFT spectrum subplot")
    parser.add_argument("--buffer", type=int, default=1000, help="Samples to display")
    return parser.parse_args()


def main():
    args = parse_args()
    port = args.port

    if not port:
        found = find_esp_ports()
        if not found:
            print("No ESP port found. Specify manually:")
            print("  raw_reader.py COM3")
            sys.exit(1)
        port = found[0]
        print(f"Auto-detected: {port}")

    ser = serial.Serial(port, args.baud, timeout=0.1)
    time.sleep(0.2)
    ser.reset_input_buffer()

    ir_buf = deque(maxlen=args.buffer)
    red_buf = deque(maxlen=args.buffer)

    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(12, 6), gridspec_kw={"height_ratios": [3, 1]})
    fig.tight_layout(pad=2.0)

    (line_ir,) = ax1.plot([], [], label="IR", alpha=0.7, lw=0.8)
    (line_red,) = ax1.plot([], [], label="RED", alpha=0.9, lw=1.0)
    ax1.set_ylabel("Amplitude")
    ax1.legend(loc="upper right")
    ax1.grid(True, alpha=0.3)

    fft_line = None
    if args.fft:
        fft_line, = ax2.plot([], [], color="red", lw=1.0)
        ax2.set_xlim(0, 50)
        ax2.set_ylabel("Magnitude")
        ax2.set_xlabel("Frequency (Hz)")
        ax2.grid(True, alpha=0.3)
    else:
        ax2.axis("off")

    samples = 0
    start = time.perf_counter()

    def update(frame):
        nonlocal samples, start

        while ser.in_waiting:
            line = ser.readline()
            try:
                line_str = line.decode("utf-8").strip()
            except UnicodeDecodeError:
                continue
            if not line_str.startswith("IR,RED,"):
                continue
            parts = line_str.split(",")
            if len(parts) != 3:
                continue
            try:
                ir = float(parts[1])
                red = float(parts[2])
            except ValueError:
                continue

            ir_buf.append(ir)
            red_buf.append(red)
            samples += 1

        if not ir_buf:
            return line_ir, line_red, *(fft_line,) if args.fft else ()

        x = list(range(len(ir_buf)))
        line_ir.set_data(x, list(ir_buf))
        line_red.set_data(x, list(red_buf))

        y_min = min(min(ir_buf), min(red_buf)) if ir_buf else 0
        y_max = max(max(ir_buf), max(red_buf)) if ir_buf else 1
        margin = (y_max - y_min) * 0.1 if y_max != y_min else 1
        ax1.set_xlim(0, len(ir_buf))
        ax1.set_ylim(y_min - margin, y_max + margin)

        elapsed = time.perf_counter() - start
        rate = samples / elapsed if elapsed > 0 else 0
        ax1.set_title(f"PPG Data | {samples} samples | {rate:.0f} Hz | port={port}")

        if args.fft and len(red_buf) >= 512:
            arr = np.array(red_buf)
            n = min(512, len(arr))
            window = np.hanning(n)
            y = arr[-n:] * window
            if n < 512:
                y = np.pad(y, (0, 512 - n))
            spectrum = np.abs(np.fft.rfft(y))[:256]
            f = np.linspace(0, 50, len(spectrum))
            fft_line.set_data(f, spectrum)
            ax2.relim()
            ax2.autoscale_view(scaley=True)

        return line_ir, line_red, *(fft_line,) if args.fft else ()

    ani = FuncAnimation(fig, update, interval=30, blit=True, cache_frame_data=False)
    plt.show()
    ser.close()


if __name__ == "__main__":
    main()
