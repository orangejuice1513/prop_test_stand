#!/usr/bin/env python3
# =============================================================================
#  listen_loadcell.py - Minimal UDP listener for test_loadcell_wifi.cpp
# =============================================================================

import re
import socket
import sys
import time

UDP_PORT = 4210
BIND_IP  = "0.0.0.0"     # listen on every interface (incl. the ESP32 AP one)

# Tolerant parser: pulls out the two integers no matter what surrounds them.
_LINE_RE = re.compile(r"T1:\s*(-?\d+)\s*\|\s*T2:\s*(-?\d+)")

def main() -> int:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    except OSError:
        pass

    try:
        sock.bind((BIND_IP, UDP_PORT))
    except OSError as e:
        print(f"[fatal] could not bind UDP {UDP_PORT}: {e}", file=sys.stderr)
        print("        - is another copy of this script already running?",
              file=sys.stderr)
        return 1

    sock.settimeout(1.0)
    print(f"[listen] UDP {BIND_IP}:{UDP_PORT} - waiting for packets...")
    print("         (make sure your laptop is joined to the 'ThrustStand' WiFi)")
    print()

    first = True
    n_packets = 0
    t0 = time.monotonic()
    last_announce = t0

    while True:
        try:
            data, addr = sock.recvfrom(512)
        except socket.timeout:
            if first and time.monotonic() - last_announce > 3.0:
                last_announce = time.monotonic()
                print("[listen] ... no packets yet. Check:")
                print("         - laptop is on 'ThrustStand' WiFi")
                print("         - ESP32 USB serial shows T1/T2 lines printing")
            continue
        except KeyboardInterrupt:
            break

        if first:
            # display T1 and T2 data 
            print(f"[listen] first packet from {addr[0]}:{addr[1]}")
            print(f"{'T1 (raw)':>14} {'T2 (raw)':>14} {'sum':>14}")
            first = False

        n_packets += 1
        line = data.decode("ascii", errors="replace").strip()
        m = _LINE_RE.search(line)
        if not m:
            print(f"[warn] unexpected format: {line!r}")
            continue

        t1 = int(m.group(1))
        t2 = int(m.group(2))
        print(f"{t1:14d} {t2:14d} {t1 + t2:14d}")

    print()
    dt = time.monotonic() - t0
    rate = n_packets / dt if dt > 0 else 0
    print(f"[listen] {n_packets} packets in {dt:.1f} s  ({rate:.1f} Hz)")
    return 0

if __name__ == "__main__":
    sys.exit(main())
