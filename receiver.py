#!/usr/bin/env python3
# =============================================================================
#  receiver.py - Propeller Test Stand ground station
# =============================================================================
#  Listens for UDP telemetry from the ESP32 on port 9000, logs every row to
#  a timestamped CSV, prints a live readout, and accepts keyboard commands
#  that are echoed back to the ESP32 on port 9001.
#
#  After a RAMP test, it auto-generates four matplotlib plots:
#      1. thrust vs throttle
#      2. thrust vs current   (efficiency curve)
#      3. thrust vs RPM
#      4. temperature vs time
#
#  CSV columns (must match telemetry.cpp):
#      cell1,cell2,total,diff,throttle_pct,rpm,v,a,temp,millis
#
#  Usage:
#      python3 receiver.py                      # default ports
#      python3 receiver.py --esp 192.168.1.42   # ESP32 address
#      python3 receiver.py --port 9000
#
#  Keyboard commands (type and hit enter):
#      arm, kill, tare, cal, ramp, t<0-100>   e.g. t50
#      plot   - re-plot the current log (handy after RAMP completes)
#      quit   - exit cleanly
# =============================================================================

import argparse
import csv
import datetime as dt
import os
import socket
import sys
import threading
import time
from dataclasses import dataclass
from typing import List, Optional

CSV_COLS = [
    "cell1", "cell2", "total", "diff",
    "throttle_pct", "rpm",
    "voltage", "current", "temp",
    "esp_millis", "host_time",
]


@dataclass
class Row:
    cell1: float
    cell2: float
    total: float
    diff: float
    throttle_pct: float
    rpm: int
    voltage: float
    current: float
    temp: float
    esp_millis: int
    host_time: float


# ---------------------------------------------------------------------------
#  State shared between the UDP receive thread and the input thread
# ---------------------------------------------------------------------------
class Station:
    def __init__(self, esp_ip: str, send_port: int, recv_port: int, log_path: str):
        self.esp_ip = esp_ip
        self.send_port = send_port
        self.recv_port = recv_port

        self.rows: List[Row] = []
        self.ramp_active = False
        self.ramp_rows: List[Row] = []
        self.last_throttle_pct = 0.0
        self.running = True

        # UDP socket for receiving from the ESP32
        self.rx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.rx.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.rx.bind(("0.0.0.0", recv_port))
        self.rx.settimeout(0.25)

        # UDP socket for sending commands to the ESP32
        self.tx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

        # CSV log
        self.log_path = log_path
        self.log_file = open(log_path, "w", newline="")
        self.writer = csv.writer(self.log_file)
        self.writer.writerow(CSV_COLS)
        print(f"[station] logging to {log_path}")

    # -------------------------------------------------------------------
    def send_command(self, cmd: str) -> None:
        data = cmd.strip().encode("ascii")
        if not data:
            return
        self.tx.sendto(data, (self.esp_ip, self.send_port))
        print(f"[station] TX -> {self.esp_ip}:{self.send_port}  \"{cmd.strip()}\"")

        up = cmd.strip().upper()
        if up == "RAMP":
            self.ramp_active = True
            self.ramp_rows = []
            print("[station] RAMP recording window opened")

    # -------------------------------------------------------------------
    def parse_line(self, line: str) -> Optional[Row]:
        parts = line.strip().split(",")
        if len(parts) != 10:
            return None
        try:
            return Row(
                cell1=float(parts[0]),
                cell2=float(parts[1]),
                total=float(parts[2]),
                diff=float(parts[3]),
                throttle_pct=float(parts[4]),
                rpm=int(float(parts[5])),
                voltage=float(parts[6]),
                current=float(parts[7]),
                temp=float(parts[8]),
                esp_millis=int(float(parts[9])),
                host_time=time.time(),
            )
        except ValueError:
            return None

    # -------------------------------------------------------------------
    def rx_loop(self) -> None:
        last_print = 0.0
        while self.running:
            try:
                pkt, _ = self.rx.recvfrom(1024)
            except socket.timeout:
                continue
            except OSError:
                break

            row = self.parse_line(pkt.decode("ascii", errors="replace"))
            if row is None:
                continue

            self.rows.append(row)
            self.writer.writerow([
                row.cell1, row.cell2, row.total, row.diff,
                row.throttle_pct, row.rpm,
                row.voltage, row.current, row.temp,
                row.esp_millis, f"{row.host_time:.3f}",
            ])
            self.log_file.flush()

            # Ramp capture: once the ESP reports throttle back at 0 after
            # having climbed, we end the window and trigger plots.
            if self.ramp_active:
                self.ramp_rows.append(row)
                if (len(self.ramp_rows) > 30 and
                        row.throttle_pct <= 0.1 and
                        self.last_throttle_pct > 1.0):
                    self.ramp_active = False
                    print("[station] RAMP complete, drawing plots...")
                    try:
                        plot_ramp(self.ramp_rows)
                    except Exception as ex:
                        print(f"[station] plot failed: {ex}")

            self.last_throttle_pct = row.throttle_pct

            # 5 Hz live readout to the terminal
            now = time.time()
            if now - last_print >= 0.2:
                last_print = now
                print(
                    f"\r[{row.esp_millis/1000:7.2f}s] "
                    f"thr={row.throttle_pct:5.1f}%  "
                    f"total={row.total:+8.1f}g  "
                    f"({row.cell1:+7.1f}/{row.cell2:+7.1f})  "
                    f"RPM={row.rpm:6d}  "
                    f"{row.voltage:5.2f}V {row.current:6.2f}A "
                    f"{row.temp:5.1f}C        ",
                    end="", flush=True,
                )

    # -------------------------------------------------------------------
    def close(self) -> None:
        self.running = False
        try:
            self.rx.close()
        except OSError:
            pass
        try:
            self.tx.close()
        except OSError:
            pass
        try:
            self.log_file.close()
        except Exception:
            pass


# ---------------------------------------------------------------------------
#  Plotting
# ---------------------------------------------------------------------------
def plot_ramp(rows: List[Row]) -> None:
    # Import matplotlib lazily so the receiver can run on a minimal Python
    # install when you only need CSV logging.
    import matplotlib.pyplot as plt

    t0 = rows[0].esp_millis / 1000.0
    t = [r.esp_millis / 1000.0 - t0 for r in rows]
    throttle = [r.throttle_pct for r in rows]
    thrust = [r.total for r in rows]
    current = [r.current for r in rows]
    rpm = [r.rpm for r in rows]
    temp = [r.temp for r in rows]

    fig, axs = plt.subplots(2, 2, figsize=(12, 8))

    axs[0, 0].scatter(throttle, thrust, s=10)
    axs[0, 0].set_xlabel("Throttle (%)")
    axs[0, 0].set_ylabel("Thrust (g)")
    axs[0, 0].set_title("Thrust vs Throttle")
    axs[0, 0].grid(True)

    # g/A efficiency curve - thrust-to-current relationship
    axs[0, 1].scatter(current, thrust, s=10, c="tab:orange")
    axs[0, 1].set_xlabel("Current (A)")
    axs[0, 1].set_ylabel("Thrust (g)")
    axs[0, 1].set_title("Thrust vs Current (efficiency)")
    axs[0, 1].grid(True)

    axs[1, 0].scatter(rpm, thrust, s=10, c="tab:green")
    axs[1, 0].set_xlabel("RPM")
    axs[1, 0].set_ylabel("Thrust (g)")
    axs[1, 0].set_title("Thrust vs RPM")
    axs[1, 0].grid(True)

    axs[1, 1].plot(t, temp, c="tab:red")
    axs[1, 1].set_xlabel("Time (s)")
    axs[1, 1].set_ylabel("MOSFET temp (C)")
    axs[1, 1].set_title("Temperature vs Time")
    axs[1, 1].grid(True)

    fig.tight_layout()
    out = os.path.splitext(os.path.basename(sys.argv[0]))[0] + "_ramp.png"
    fig.savefig(out, dpi=150)
    print(f"\n[station] plots saved to {out}")
    plt.show()


# ---------------------------------------------------------------------------
#  Input thread
# ---------------------------------------------------------------------------
HELP = (
    "commands: arm | kill | tare | cal | ramp | t<N> | plot | quit\n"
)


def input_loop(station: Station) -> None:
    print(HELP, end="")
    while station.running:
        try:
            line = input("> ").strip()
        except EOFError:
            station.running = False
            return
        if not line:
            continue

        low = line.lower()
        if low in ("quit", "exit", "q"):
            station.running = False
            return
        if low == "help":
            print(HELP, end="")
            continue
        if low == "plot":
            if not station.rows:
                print("[station] no rows captured yet")
            else:
                try:
                    plot_ramp(station.rows)
                except Exception as ex:
                    print(f"[station] plot failed: {ex}")
            continue

        # Everything else passes straight through to the ESP32.
        station.send_command(line)


# ---------------------------------------------------------------------------
def main() -> None:
    ap = argparse.ArgumentParser(description="Prop test stand ground station")
    ap.add_argument("--esp", default="192.168.1.100",
                    help="ESP32 IP address (for sending commands)")
    ap.add_argument("--cmd-port", type=int, default=9001,
                    help="UDP port to send commands to on the ESP32")
    ap.add_argument("--port", type=int, default=9000,
                    help="UDP port to listen on for telemetry")
    args = ap.parse_args()

    stamp = dt.datetime.now().strftime("%Y%m%d_%H%M%S")
    log_path = f"proptest_{stamp}.csv"

    station = Station(
        esp_ip=args.esp,
        send_port=args.cmd_port,
        recv_port=args.port,
        log_path=log_path,
    )

    rx_thread = threading.Thread(target=station.rx_loop, daemon=True)
    rx_thread.start()

    try:
        input_loop(station)
    except KeyboardInterrupt:
        pass
    finally:
        # On exit, send a KILL to be safe.
        try:
            station.send_command("KILL")
        except Exception:
            pass
        station.close()
        print("\n[station] bye.")


if __name__ == "__main__":
    main()
