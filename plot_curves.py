#!/usr/bin/env python3
"""
plot_curves.py: plot analysis curves from receiver.py CSV logs.

Expects CSV columns written by receiver.py:
  cell1,cell2,total,diff,throttle_pct,rpm,voltage,current,temp,esp_millis,host_time

Outputs a single 2x3 figure containing:
  - Efficiency map (g/W vs thrust)
  - Voltage vs current (+ simple internal resistance fit)
  - Vibration proxy (stddev thrust vs RPM)
  - Step response (thrust vs time, if a 0->100% step is detected)
  - Thrust vs RPM^2
  - Thrust vs current
"""

from __future__ import annotations

import argparse
import csv
import os
from dataclasses import dataclass
from typing import List, Optional, Tuple


@dataclass
class Row:
    thrust_g: float
    current_a: float
    voltage_v: Optional[float]
    throttle_pct: float
    rpm: Optional[float]
    esp_millis: Optional[float]


def _to_float(s: str) -> Optional[float]:
    try:
        return float(s)
    except Exception:
        return None


def load_rows(path: str) -> List[Row]:
    rows: List[Row] = []
    with open(path, "r", newline="") as f:
        reader = csv.DictReader(f)
        for r in reader:
            thrust = _to_float(r.get("total", ""))
            current = _to_float(r.get("current", ""))
            throttle = _to_float(r.get("throttle_pct", ""))
            voltage = _to_float(r.get("voltage", ""))
            if thrust is None or current is None or throttle is None:
                continue

            rows.append(
                Row(
                    thrust_g=thrust,
                    current_a=current,
                    voltage_v=voltage,
                    throttle_pct=throttle,
                    rpm=_to_float(r.get("rpm", "")),
                    esp_millis=_to_float(r.get("esp_millis", "")),
                )
            )
    return rows


def filter_rows(
    rows: List[Row],
    *,
    min_throttle: float,
    max_throttle: float,
    min_current: float,
    max_current: float,
    min_thrust: float,
    max_thrust: float,
) -> List[Row]:
    out: List[Row] = []
    for r in rows:
        if not (min_throttle <= r.throttle_pct <= max_throttle):
            continue
        if not (min_current <= r.current_a <= max_current):
            continue
        if not (min_thrust <= r.thrust_g <= max_thrust):
            continue
        out.append(r)
    return out


def binned_curve_xy(xs: List[float], ys: List[float], bins: int) -> Tuple[List[float], List[float]]:
    if not xs or not ys or bins <= 1 or len(xs) != len(ys):
        return ([], [])

    x_min = min(xs)
    x_max = max(xs)
    if x_max <= x_min:
        return ([], [])

    step = (x_max - x_min) / bins
    sums = [0.0] * bins
    counts = [0] * bins

    for x, y in zip(xs, ys):
        i = int((x - x_min) / step)
        if i == bins:
            i = bins - 1
        if 0 <= i < bins:
            sums[i] += y
            counts[i] += 1

    curve_x: List[float] = []
    curve_y: List[float] = []
    for i in range(bins):
        if counts[i] == 0:
            continue
        center = x_min + (i + 0.5) * step
        curve_x.append(center)
        curve_y.append(sums[i] / counts[i])
    return (curve_x, curve_y)


def _safe_power_w(v: Optional[float], a: float) -> Optional[float]:
    if v is None:
        return None
    p = v * a
    if p <= 0:
        return None
    return p


def _linear_fit_r_internal(rows: List[Row]) -> Optional[Tuple[float, float]]:
    """
    Fit V = V0 - I*R using least squares.
    Returns (R_ohm, V0_volts) or None if insufficient valid points.
    """
    xs: List[float] = []
    ys: List[float] = []
    for r in rows:
        if r.voltage_v is None:
            continue
        xs.append(r.current_a)
        ys.append(r.voltage_v)

    n = len(xs)
    if n < 3:
        return None

    x_mean = sum(xs) / n
    y_mean = sum(ys) / n
    sxx = sum((x - x_mean) ** 2 for x in xs)
    if sxx <= 0:
        return None
    sxy = sum((x - x_mean) * (y - y_mean) for x, y in zip(xs, ys))
    slope = sxy / sxx  # V per A
    intercept = y_mean - slope * x_mean
    r_ohm = -slope
    return (r_ohm, intercept)


def _rolling_std(values: List[float], window: int) -> List[Optional[float]]:
    if window <= 1:
        return [None] * len(values)
    out: List[Optional[float]] = [None] * len(values)
    for i in range(len(values)):
        j0 = max(0, i - window + 1)
        w = values[j0 : i + 1]
        if len(w) < 2:
            continue
        m = sum(w) / len(w)
        var = sum((x - m) ** 2 for x in w) / (len(w) - 1)
        out[i] = var ** 0.5
    return out


def _find_step_segment(rows: List[Row], low: float, high: float) -> Optional[Tuple[int, int]]:
    for i in range(1, len(rows)):
        if rows[i - 1].throttle_pct <= low and rows[i].throttle_pct >= high:
            start = max(0, i - 10)
            end = min(len(rows), i + 80)
            return (start, end)
    return None


def _rise_time_ms(rows: List[Row]) -> Optional[float]:
    ts = [r.esp_millis for r in rows]
    if any(t is None for t in ts):
        return None
    tms = [float(t) for t in ts if t is not None]
    thrust = [r.thrust_g for r in rows]
    if len(thrust) < 6:
        return None
    n0 = max(1, min(10, len(thrust) // 5))
    baseline = sum(thrust[:n0]) / n0
    final = sum(thrust[-n0:]) / n0
    delta = final - baseline
    if delta <= 0:
        return None
    y10 = baseline + 0.1 * delta
    y90 = baseline + 0.9 * delta

    t10 = None
    t90 = None
    for t, y in zip(tms, thrust):
        if t10 is None and y >= y10:
            t10 = t
        if t10 is not None and y >= y90:
            t90 = t
            break
    if t10 is None or t90 is None:
        return None
    return t90 - t10


def main() -> None:
    ap = argparse.ArgumentParser(description="Plot analysis curves from proptest CSV logs")
    ap.add_argument("csv", help="CSV log from receiver.py (e.g. proptest_YYYYMMDD_HHMMSS.csv)")
    ap.add_argument("--out", default=None, help="Output image path (default: <csv stem>_analysis.png)")
    ap.add_argument("--bins", type=int, default=40, help="Bins for binned-mean curve overlays (0 to disable)")
    ap.add_argument("--std-window", type=int, default=15, help="Window (samples) for rolling std dev")
    ap.add_argument("--min-throttle", type=float, default=0.0)
    ap.add_argument("--max-throttle", type=float, default=100.0)
    ap.add_argument("--min-current", type=float, default=float("-inf"))
    ap.add_argument("--max-current", type=float, default=float("inf"))
    ap.add_argument("--min-thrust", type=float, default=float("-inf"))
    ap.add_argument("--max-thrust", type=float, default=float("inf"))
    args = ap.parse_args()

    rows = load_rows(args.csv)
    rows = filter_rows(
        rows,
        min_throttle=args.min_throttle,
        max_throttle=args.max_throttle,
        min_current=args.min_current,
        max_current=args.max_current,
        min_thrust=args.min_thrust,
        max_thrust=args.max_thrust,
    )

    if not rows:
        raise SystemExit("No rows to plot (check file/column names/filters).")

    import matplotlib.pyplot as plt

    fig, axs = plt.subplots(2, 3, figsize=(16, 9))
    fig.suptitle("Prop test stand analysis")

    # Efficiency map (g/W vs thrust)
    eff_x: List[float] = []
    eff_y: List[float] = []
    for r in rows:
        p = _safe_power_w(r.voltage_v, r.current_a)
        if p is None:
            continue
        eff = r.thrust_g / p
        if eff >= 0:
            eff_x.append(r.thrust_g)
            eff_y.append(eff)
    ax = axs[0, 0]
    if eff_x:
        ax.scatter(eff_x, eff_y, s=10, alpha=0.5)
        if args.bins and args.bins > 1:
            cx, cy = binned_curve_xy(eff_x, eff_y, args.bins)
            if cx:
                ax.plot(cx, cy, linewidth=2.0)
    ax.set_xlabel("Thrust (g)")
    ax.set_ylabel("Efficiency (g/W)")
    ax.set_title("Efficiency map (g/W vs Thrust)")
    ax.grid(True)

    # Voltage sag (V vs I) + internal resistance estimate
    ax = axs[0, 1]
    vxs: List[float] = []
    vys: List[float] = []
    for r in rows:
        if r.voltage_v is None:
            continue
        vxs.append(r.current_a)
        vys.append(r.voltage_v)
    if vxs:
        ax.scatter(vxs, vys, s=10, alpha=0.5)
    fit = _linear_fit_r_internal(rows)
    if fit and vxs:
        r_ohm, v0 = fit
        x0 = min(vxs)
        x1 = max(vxs)
        ax.plot([x0, x1], [v0 - r_ohm * x0, v0 - r_ohm * x1], linewidth=2.0)
        ax.text(
            0.02,
            0.02,
            f"fit: V = {v0:.2f} - I*{r_ohm:.4f}  (R≈{r_ohm*1000:.1f} mΩ)",
            transform=ax.transAxes,
            fontsize=9,
            verticalalignment="bottom",
        )
    ax.set_xlabel("Current (A)")
    ax.set_ylabel("Voltage (V)")
    ax.set_title("Voltage sag (V vs Current)")
    ax.grid(True)

    # Mechanical resonance proxy: rolling stddev(thrust) vs RPM
    ax = axs[0, 2]
    rpms: List[float] = []
    thrusts_for_std: List[float] = []
    for r in rows:
        if r.rpm is None or r.rpm <= 0:
            continue
        rpms.append(float(r.rpm))
        thrusts_for_std.append(r.thrust_g)
    std_series = _rolling_std(thrusts_for_std, args.std_window)
    pairs = [(rpm, s) for rpm, s in zip(rpms, std_series) if s is not None]
    if pairs:
        ax.scatter([p[0] for p in pairs], [p[1] for p in pairs], s=10, alpha=0.5)
    ax.set_xlabel("RPM (eRPM in firmware)")
    ax.set_ylabel("Thrust std dev (g)")
    ax.set_title("Vibration proxy (stddev thrust vs RPM)")
    ax.grid(True)

    # Step response (thrust vs time)
    ax = axs[1, 0]
    seg = _find_step_segment(rows, low=5.0, high=95.0)
    if seg:
        i0, i1 = seg
        seg_rows = rows[i0:i1]
        if all(r.esp_millis is not None for r in seg_rows):
            t0 = float(seg_rows[0].esp_millis)
            t = [(float(r.esp_millis) - t0) / 1000.0 for r in seg_rows]
            y = [r.thrust_g for r in seg_rows]
            ax.plot(t, y, linewidth=1.5)
            tr = _rise_time_ms(seg_rows)
            title = "Step response (thrust vs time)"
            if tr is not None:
                title += f"  (t_r≈{tr:.0f} ms)"
            ax.set_title(title)
        else:
            ax.set_title("Step response (missing esp_millis)")
    else:
        ax.set_title("Step response (no 0→100% throttle step found)")
    ax.set_xlabel("Time (s)")
    ax.set_ylabel("Thrust (g)")
    ax.grid(True)

    # Prop characterization: thrust vs RPM^2
    ax = axs[1, 1]
    x_r2: List[float] = []
    y_th: List[float] = []
    for r in rows:
        if r.rpm is None or r.rpm <= 0:
            continue
        x_r2.append(float(r.rpm) ** 2)
        y_th.append(r.thrust_g)
    if x_r2:
        ax.scatter(x_r2, y_th, s=10, alpha=0.5)
        if args.bins and args.bins > 1:
            cx, cy = binned_curve_xy(x_r2, y_th, args.bins)
            if cx:
                ax.plot(cx, cy, linewidth=2.0)
    ax.set_xlabel("RPM^2")
    ax.set_ylabel("Thrust (g)")
    ax.set_title("Prop characterization (Thrust vs RPM^2)")
    ax.grid(True)

    # Thrust vs current (reference)
    ax = axs[1, 2]
    cur = [r.current_a for r in rows]
    thr = [r.thrust_g for r in rows]
    ax.scatter(cur, thr, s=10, alpha=0.5)
    if args.bins and args.bins > 1:
        cx, cy = binned_curve_xy(cur, thr, args.bins)
        if cx:
            ax.plot(cx, cy, linewidth=2.0)
    ax.set_xlabel("Current (A)")
    ax.set_ylabel("Thrust (g)")
    ax.set_title("Thrust vs Current")
    ax.grid(True)

    out = args.out
    if not out:
        stem, _ = os.path.splitext(os.path.basename(args.csv))
        out = f"{stem}_analysis.png"
    fig.tight_layout(rect=(0, 0.02, 1, 0.96))
    fig.savefig(out, dpi=150)
    print(out)
    plt.show()


if __name__ == "__main__":
    main()

