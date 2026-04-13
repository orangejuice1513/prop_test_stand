"""
read_data.py — receive HX711 load cell data from ESP32 over WiFi (UDP)

Usage:
    python read_data.py [--port 4210] [--output run1.csv]

The ESP32 sends UDP packets containing CSV: timestamp_ms,left,right
We compute:
    total_thrust = left + right
    off_center   = left - right
"""

import argparse
import csv
import socket

COLUMNS = ["timestamp_ms", "left", "right", "total_thrust", "off_center"]


def listen(port: int, output_path: str | None):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("0.0.0.0", port))   # listen on all interfaces
    sock.settimeout(5.0)
    print(f"Listening for UDP packets on port {port} (Ctrl-C to stop)")

    out_file = open(output_path, "w", newline="") if output_path else None
    writer = csv.writer(out_file) if out_file else None
    if writer:
        writer.writerow(COLUMNS)
        print(f"Saving to {output_path}")

    try:
        while True:
            try:
                data, addr = sock.recvfrom(1024)
            except TimeoutError:
                print("[warn] no packets in 5 s — is the ESP32 running and pointing at this IP?")
                continue

            line = data.decode("utf-8", errors="replace").strip()
            fields = line.split(",")
            if len(fields) != 3:
                print(f"[warn] bad packet from {addr[0]}: {line!r}")
                continue

            try:
                t_ms = int(fields[0])
                left = float(fields[1])
                right = float(fields[2])
            except ValueError:
                print(f"[warn] parse error: {line!r}")
                continue

            total = left + right       # total thrust
            off   = left - right       # off-center (positive = left-heavy)

            print(f"t={t_ms:>8} ms  L={left:+8.3f}  R={right:+8.3f}  "
                  f"total={total:+8.3f}  off={off:+8.3f}")

            if writer:
                writer.writerow([t_ms, left, right, total, off])

    except KeyboardInterrupt:
        print("\nStopped.")
    finally:
        sock.close()
        if out_file:
            out_file.close()
            print(f"Data saved to {output_path}")


def main():
    parser = argparse.ArgumentParser(description="Receive ESP32 load cell data (UDP)")
    parser.add_argument("--port", type=int, default=4210, help="UDP port (must match ESP32 firmware)")
    parser.add_argument("--output", default=None, help="Optional CSV file to save data")
    args = parser.parse_args()

    listen(args.port, args.output)


if __name__ == "__main__":
    main()
