#!/usr/bin/env python3
# =============================================================================
#  ground_station.py - Real-time GUI for the propeller test stand
# =============================================================================
#  Receives UDP telemetry from the ESP32 on port 9000, streams it into a
#  Tkinter + matplotlib dashboard, and sends commands back on port 9001.
#
#  Left column:  live rolling time-series (last N seconds)
#      - thrust (total g)
#      - throttle (%)
#      - RPM
#      - voltage / current
#
#  Right column: static analysis scatters that fill in as data arrives
#      - thrust vs throttle
#      - thrust vs current
#      - thrust vs RPM
#      - temperature vs time
#
#  Top bar: ARM / KILL / TARE / CAL / RAMP buttons, a throttle slider with a
#  SEND button, and a live numeric readout.
#
#  Everything is logged to proptest_YYYYMMDD_HHMMSS.csv in the same columns
#  receiver.py writes, so plot_curves.py works on these logs unchanged.
#
#  Usage:
#      python3 ground_station.py --esp 192.168.1.42
# =============================================================================

import argparse
import csv
import datetime as dt
import queue
import socket
import threading
import time
import tkinter as tk
from tkinter import ttk
from collections import deque
from dataclasses import dataclass
from typing import Deque, List, Optional

import matplotlib
matplotlib.use("TkAgg")
from matplotlib.figure import Figure
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg

CSV_COLS = [
    "cell1", "cell2", "total", "diff",
    "throttle_pct", "rpm",
    "voltage", "current", "temp",
    "esp_millis", "host_time",
]

LIVE_WINDOW_S = 30.0   # seconds of history shown in the rolling plots
UI_REFRESH_MS = 100    # GUI redraw period (10 Hz is plenty)


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
#  UDP receive thread
# ---------------------------------------------------------------------------
class Receiver(threading.Thread):
    def __init__(self, recv_port: int, out_queue: "queue.Queue[Row]"):
        super().__init__(daemon=True)
        self.out_queue = out_queue
        self.running = True
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind(("0.0.0.0", recv_port))
        self.sock.settimeout(0.25)

    def run(self) -> None:
        while self.running:
            try:
                pkt, _ = self.sock.recvfrom(1024)
            except socket.timeout:
                continue
            except OSError:
                return

            row = parse_line(pkt.decode("ascii", errors="replace"))
            if row is not None:
                self.out_queue.put(row)

    def stop(self) -> None:
        self.running = False
        try:
            self.sock.close()
        except OSError:
            pass


def parse_line(line: str) -> Optional[Row]:
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


# ---------------------------------------------------------------------------
#  Command transmitter
# ---------------------------------------------------------------------------
class Commander:
    def __init__(self, esp_ip: str, send_port: int):
        self.esp_ip = esp_ip
        self.send_port = send_port
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    def send(self, cmd: str) -> None:
        data = cmd.strip().encode("ascii")
        if not data:
            return
        self.sock.sendto(data, (self.esp_ip, self.send_port))

    def close(self) -> None:
        try:
            self.sock.close()
        except OSError:
            pass


# ---------------------------------------------------------------------------
#  Tkinter dashboard
# ---------------------------------------------------------------------------
class Dashboard:
    def __init__(self, root: tk.Tk, commander: Commander, rx_queue: "queue.Queue[Row]",
                 log_writer: csv.writer, log_file):
        self.root = root
        self.commander = commander
        self.rx_queue = rx_queue
        self.log_writer = log_writer
        self.log_file = log_file

        # Rolling buffers for the live plots (last LIVE_WINDOW_S seconds)
        self.t_buf: Deque[float] = deque()
        self.thrust_buf: Deque[float] = deque()
        self.throttle_buf: Deque[float] = deque()
        self.rpm_buf: Deque[float] = deque()
        self.voltage_buf: Deque[float] = deque()
        self.current_buf: Deque[float] = deque()

        # Full-session buffers for the static scatter plots
        self.all_throttle: List[float] = []
        self.all_thrust: List[float] = []
        self.all_rpm: List[float] = []
        self.all_current: List[float] = []
        self.all_temp: List[float] = []
        self.all_time: List[float] = []

        self.t0: Optional[float] = None
        self.last_row: Optional[Row] = None

        root.title("Propeller Test Stand - Ground Station")
        root.geometry("1400x900")
        root.protocol("WM_DELETE_WINDOW", self._on_close)

        self._build_controls()
        self._build_plots()

        self.root.after(UI_REFRESH_MS, self._tick)

    # -------------------------------------------------------------------
    def _build_controls(self) -> None:
        bar = ttk.Frame(self.root, padding=6)
        bar.pack(side=tk.TOP, fill=tk.X)

        def mkbtn(text, cmd, color=None):
            b = tk.Button(bar, text=text, command=cmd, width=7)
            if color:
                b.configure(bg=color, activebackground=color, fg="white")
            b.pack(side=tk.LEFT, padx=2)
            return b

        mkbtn("ARM",  lambda: self._send("ARM"),  color="#2e7d32")
        mkbtn("KILL", lambda: self._send("KILL"), color="#c62828")
        mkbtn("TARE", lambda: self._send("TARE"))
        mkbtn("CAL",  lambda: self._send("CAL"))
        mkbtn("RAMP", lambda: self._send("RAMP"))

        ttk.Separator(bar, orient=tk.VERTICAL).pack(side=tk.LEFT, fill=tk.Y, padx=8)

        ttk.Label(bar, text="Throttle:").pack(side=tk.LEFT)
        self.throttle_var = tk.IntVar(value=0)
        self.throttle_scale = ttk.Scale(
            bar, from_=0, to=100, orient=tk.HORIZONTAL,
            variable=self.throttle_var, length=240,
            command=lambda v: self.throttle_label.configure(text=f"{int(float(v)):3d}%"),
        )
        self.throttle_scale.pack(side=tk.LEFT, padx=4)
        self.throttle_label = ttk.Label(bar, text="  0%", width=5)
        self.throttle_label.pack(side=tk.LEFT)
        mkbtn("SEND", self._send_throttle)
        mkbtn("ZERO", lambda: (self.throttle_var.set(0),
                               self.throttle_label.configure(text="  0%"),
                               self._send("T0")))

        ttk.Separator(bar, orient=tk.VERTICAL).pack(side=tk.LEFT, fill=tk.Y, padx=8)

        self.status_var = tk.StringVar(value="waiting for telemetry...")
        ttk.Label(bar, textvariable=self.status_var, font=("TkDefaultFont", 10, "bold")) \
            .pack(side=tk.LEFT, padx=4)

    def _send(self, cmd: str) -> None:
        self.commander.send(cmd)

    def _send_throttle(self) -> None:
        v = int(self.throttle_var.get())
        self._send(f"T{v}")

    # -------------------------------------------------------------------
    def _build_plots(self) -> None:
        self.fig = Figure(figsize=(14, 8), dpi=100)
        self.fig.subplots_adjust(
            left=0.06, right=0.98, top=0.95, bottom=0.06,
            wspace=0.30, hspace=0.45,
        )

        # --- LIVE column (left) --------------------------------------------
        self.ax_thrust_t   = self.fig.add_subplot(4, 2, 1)
        self.ax_throttle_t = self.fig.add_subplot(4, 2, 3)
        self.ax_rpm_t      = self.fig.add_subplot(4, 2, 5)
        self.ax_va_t       = self.fig.add_subplot(4, 2, 7)

        self.ln_thrust,   = self.ax_thrust_t.plot([], [], lw=1.5, color="tab:blue")
        self.ln_throttle, = self.ax_throttle_t.plot([], [], lw=1.5, color="tab:purple")
        self.ln_rpm,      = self.ax_rpm_t.plot([], [], lw=1.5, color="tab:green")
        self.ln_voltage,  = self.ax_va_t.plot([], [], lw=1.5, color="tab:red", label="V")
        self.ax_va_i = self.ax_va_t.twinx()
        self.ln_current,  = self.ax_va_i.plot([], [], lw=1.5, color="tab:orange", label="A")

        for ax, ylabel, title in (
            (self.ax_thrust_t,   "Thrust (g)",   "Live: Thrust"),
            (self.ax_throttle_t, "Throttle (%)", "Live: Throttle"),
            (self.ax_rpm_t,      "RPM",          "Live: RPM"),
            (self.ax_va_t,       "Voltage (V)",  "Live: Voltage / Current"),
        ):
            ax.set_title(title, fontsize=10)
            ax.set_ylabel(ylabel, fontsize=9)
            ax.grid(True, alpha=0.4)
        self.ax_va_t.set_xlabel("Time (s)", fontsize=9)
        self.ax_va_i.set_ylabel("Current (A)", fontsize=9)

        # --- STATIC column (right) -----------------------------------------
        self.ax_thrust_throttle = self.fig.add_subplot(4, 2, 2)
        self.ax_thrust_current  = self.fig.add_subplot(4, 2, 4)
        self.ax_thrust_rpm      = self.fig.add_subplot(4, 2, 6)
        self.ax_temp_time       = self.fig.add_subplot(4, 2, 8)

        self.sc_throttle = self.ax_thrust_throttle.scatter([], [], s=8, c="tab:blue",   alpha=0.6)
        self.sc_current  = self.ax_thrust_current.scatter([],  [], s=8, c="tab:orange", alpha=0.6)
        self.sc_rpm      = self.ax_thrust_rpm.scatter([],      [], s=8, c="tab:green",  alpha=0.6)
        self.ln_temp,    = self.ax_temp_time.plot([], [], lw=1.2, color="tab:red")

        for ax, xlabel, ylabel, title in (
            (self.ax_thrust_throttle, "Throttle (%)", "Thrust (g)",    "Static: Thrust vs Throttle"),
            (self.ax_thrust_current,  "Current (A)",  "Thrust (g)",    "Static: Thrust vs Current"),
            (self.ax_thrust_rpm,      "RPM",          "Thrust (g)",    "Static: Thrust vs RPM"),
            (self.ax_temp_time,       "Time (s)",     "Temp (C)",      "Static: Temperature vs Time"),
        ):
            ax.set_title(title, fontsize=10)
            ax.set_xlabel(xlabel, fontsize=9)
            ax.set_ylabel(ylabel, fontsize=9)
            ax.grid(True, alpha=0.4)

        self.canvas = FigureCanvasTkAgg(self.fig, master=self.root)
        self.canvas.get_tk_widget().pack(side=tk.TOP, fill=tk.BOTH, expand=True)
        self.canvas.draw()

    # -------------------------------------------------------------------
    def _drain_queue(self) -> int:
        n = 0
        try:
            while True:
                row = self.rx_queue.get_nowait()
                self._ingest(row)
                n += 1
        except queue.Empty:
            pass
        return n

    def _ingest(self, row: Row) -> None:
        self.last_row = row

        if self.t0 is None:
            self.t0 = row.esp_millis / 1000.0
        t = row.esp_millis / 1000.0 - self.t0

        # CSV log (matches receiver.py columns exactly)
        self.log_writer.writerow([
            row.cell1, row.cell2, row.total, row.diff,
            row.throttle_pct, row.rpm,
            row.voltage, row.current, row.temp,
            row.esp_millis, f"{row.host_time:.3f}",
        ])
        self.log_file.flush()

        # Rolling buffers
        self.t_buf.append(t)
        self.thrust_buf.append(row.total)
        self.throttle_buf.append(row.throttle_pct)
        self.rpm_buf.append(float(row.rpm))
        self.voltage_buf.append(row.voltage)
        self.current_buf.append(row.current)

        cutoff = t - LIVE_WINDOW_S
        while self.t_buf and self.t_buf[0] < cutoff:
            self.t_buf.popleft()
            self.thrust_buf.popleft()
            self.throttle_buf.popleft()
            self.rpm_buf.popleft()
            self.voltage_buf.popleft()
            self.current_buf.popleft()

        # Full-session buffers
        self.all_throttle.append(row.throttle_pct)
        self.all_thrust.append(row.total)
        self.all_rpm.append(float(row.rpm))
        self.all_current.append(row.current)
        self.all_temp.append(row.temp)
        self.all_time.append(t)

    # -------------------------------------------------------------------
    def _tick(self) -> None:
        got = self._drain_queue()
        if got and self.last_row is not None:
            self._redraw()
            r = self.last_row
            self.status_var.set(
                f"t={r.esp_millis/1000:7.2f}s   "
                f"thr={r.throttle_pct:5.1f}%   "
                f"thrust={r.total:+7.1f}g   "
                f"RPM={r.rpm:6d}   "
                f"{r.voltage:5.2f}V {r.current:6.2f}A   "
                f"{r.temp:5.1f}C"
            )
        self.root.after(UI_REFRESH_MS, self._tick)

    # -------------------------------------------------------------------
    def _redraw(self) -> None:
        # --- Live rolling plots -----------------------------------------
        tvals = list(self.t_buf)

        self.ln_thrust.set_data(tvals, list(self.thrust_buf))
        self.ln_throttle.set_data(tvals, list(self.throttle_buf))
        self.ln_rpm.set_data(tvals, list(self.rpm_buf))
        self.ln_voltage.set_data(tvals, list(self.voltage_buf))
        self.ln_current.set_data(tvals, list(self.current_buf))

        if tvals:
            t_min, t_max = tvals[0], tvals[-1]
            if t_max - t_min < 1.0:
                t_max = t_min + 1.0
            for ax in (self.ax_thrust_t, self.ax_throttle_t,
                       self.ax_rpm_t, self.ax_va_t):
                ax.set_xlim(t_min, t_max)
            self.ax_va_i.set_xlim(t_min, t_max)

            _autoscale_y(self.ax_thrust_t, self.thrust_buf)
            self.ax_throttle_t.set_ylim(-5, 105)
            _autoscale_y(self.ax_rpm_t, self.rpm_buf)
            _autoscale_y(self.ax_va_t, self.voltage_buf)
            _autoscale_y(self.ax_va_i, self.current_buf)

        # --- Static scatters (grow over the session) --------------------
        if self.all_throttle:
            self.sc_throttle.set_offsets(list(zip(self.all_throttle, self.all_thrust)))
            _autoscale_scatter(self.ax_thrust_throttle,
                               self.all_throttle, self.all_thrust,
                               x_pad=2.0, y_pad_pct=0.08)

            self.sc_current.set_offsets(list(zip(self.all_current, self.all_thrust)))
            _autoscale_scatter(self.ax_thrust_current,
                               self.all_current, self.all_thrust,
                               x_pad=0.2, y_pad_pct=0.08)

            self.sc_rpm.set_offsets(list(zip(self.all_rpm, self.all_thrust)))
            _autoscale_scatter(self.ax_thrust_rpm,
                               self.all_rpm, self.all_thrust,
                               x_pad=50.0, y_pad_pct=0.08)

            self.ln_temp.set_data(self.all_time, self.all_temp)
            _autoscale_scatter(self.ax_temp_time,
                               self.all_time, self.all_temp,
                               x_pad=1.0, y_pad_pct=0.08)

        self.canvas.draw_idle()

    # -------------------------------------------------------------------
    def _on_close(self) -> None:
        try:
            self.commander.send("KILL")
        except Exception:
            pass
        try:
            self.log_file.close()
        except Exception:
            pass
        self.root.quit()
        self.root.destroy()


def _autoscale_y(ax, values) -> None:
    if not values:
        return
    lo = min(values)
    hi = max(values)
    if hi - lo < 1e-6:
        lo -= 0.5
        hi += 0.5
    pad = 0.1 * (hi - lo)
    ax.set_ylim(lo - pad, hi + pad)


def _autoscale_scatter(ax, xs, ys, x_pad: float, y_pad_pct: float) -> None:
    if not xs or not ys:
        return
    x_lo, x_hi = min(xs), max(xs)
    y_lo, y_hi = min(ys), max(ys)
    if x_hi - x_lo < 1e-6:
        x_lo -= x_pad
        x_hi += x_pad
    if y_hi - y_lo < 1e-6:
        y_lo -= 1.0
        y_hi += 1.0
    y_pad = y_pad_pct * (y_hi - y_lo)
    ax.set_xlim(x_lo - x_pad, x_hi + x_pad)
    ax.set_ylim(y_lo - y_pad, y_hi + y_pad)


# ---------------------------------------------------------------------------
def main() -> None:
    ap = argparse.ArgumentParser(description="Real-time GUI for prop test stand")
    ap.add_argument("--esp", default="192.168.1.100",
                    help="ESP32 IP address (destination for commands)")
    ap.add_argument("--cmd-port", type=int, default=9001,
                    help="UDP port to send commands to on the ESP32")
    ap.add_argument("--port", type=int, default=9000,
                    help="UDP port to listen on for telemetry")
    args = ap.parse_args()

    stamp = dt.datetime.now().strftime("%Y%m%d_%H%M%S")
    log_path = f"proptest_{stamp}.csv"
    log_file = open(log_path, "w", newline="")
    writer = csv.writer(log_file)
    writer.writerow(CSV_COLS)
    print(f"[ground_station] logging to {log_path}")
    print(f"[ground_station] listening on UDP :{args.port}")
    print(f"[ground_station] commands -> {args.esp}:{args.cmd_port}")

    rx_queue: "queue.Queue[Row]" = queue.Queue()
    receiver = Receiver(args.port, rx_queue)
    receiver.start()
    commander = Commander(args.esp, args.cmd_port)

    root = tk.Tk()
    Dashboard(root, commander, rx_queue, writer, log_file)
    try:
        root.mainloop()
    finally:
        receiver.stop()
        commander.close()


if __name__ == "__main__":
    main()
