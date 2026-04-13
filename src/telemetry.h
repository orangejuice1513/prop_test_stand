#pragma once
// =============================================================================
//  telemetry.h  -  WiFi UDP bi-directional link
// =============================================================================
//  Connects to the SSID defined in config.h, streams a CSV row to the
//  host laptop at TELEM_RATE_HZ, and listens on UDP_LISTEN_PORT for
//  one-line ASCII commands from receiver.py.
//
//  CSV schema (must match receiver.py):
//
//      cell1,cell2,total,diff,throttle_pct,rpm,v,a,temp,millis
//
//  Supported commands (case-insensitive):
//      ARM        -> arm ESC (runs arm sequence again if needed)
//      KILL       -> immediate throttle=0
//      TARE       -> zero both load cells
//      T<n>       -> set throttle percent (e.g. T35 = 35%)
//      CAL        -> enter calibration sequence (blocking)
//      RAMP       -> run a scripted throttle ramp (see main.cpp)
// =============================================================================

#include <Arduino.h>

namespace telemetry {

enum class Command {
    NONE,
    ARM,
    KILL,
    TARE,
    THROTTLE,   // payload is a float percent
    CALIBRATE,
    RAMP,
};

struct Event {
    Command cmd;
    float   value;   // used by THROTTLE (percent)
};

// Snapshot of everything we want to stream each frame.
struct Sample {
    float    cell1_g;
    float    cell2_g;
    float    total_g;
    float    diff_g;
    float    throttle_pct;
    uint32_t rpm;
    float    voltage_v;
    float    current_a;
    float    temp_c;
};

// Connect WiFi and bind UDP sockets.
void begin();

// Non-blocking: send a telemetry row if TELEM_RATE_HZ-period has elapsed.
void maybe_send(const Sample& s);

// Poll the command socket. Returns {NONE, 0} if nothing waiting.
Event poll_command();

// ms since last command was received. Useful for the deadman watchdog.
uint32_t ms_since_last_command();

// Is the WiFi link currently up?
bool connected();

}  // namespace telemetry
