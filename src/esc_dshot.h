#pragma once
// =============================================================================
//  esc_dshot.h  -  DShot600 ESC driver (derdoktor667/DShotRMT)
// =============================================================================
//  DShot is a digital ESC protocol: each frame is 16 bits (11 throttle, 1
//  telemetry-request, 4 CRC). Valid throttle values are 48..2047. Values
//  0..47 are reserved commands (beep, disarm, save, 3D mode, etc).
//
//  This driver runs DShot in UNIDIRECTIONAL mode - the ESP32 transmits
//  frames but does not listen for RPM feedback on the signal line. RPM,
//  voltage, current and temperatures come from the APD "T" UART telemetry
//  wire instead (see src/esc_telem.*). That means GPIO19 does NOT need a
//  pull-up resistor.
//
//  Most ESCs time out after ~100 ms of DShot silence, so we spawn a
//  FreeRTOS task pinned to Core 0 that re-transmits the latest throttle
//  every DSHOT_PERIOD_MS (default 2 ms -> 500 Hz).
//
//  Library: https://github.com/derdoktor667/DShotRMT  (v0.9.5+ API)
// =============================================================================

#include <Arduino.h>

namespace esc_dshot {

// Initialise the RMT channel and spawn the sender task. Holds throttle=0
// (DShot command "disarm") for three seconds while the ESC boots.
void begin();

// Set raw DShot throttle value. Clamped to [DSHOT_MIN, DSHOT_MAX], or
// pass 0 for the "motor stop" reserved command.
void write_raw(uint16_t dshot_value);

// Convenience: 0..100 percent.
void write_throttle_pct(float pct);

// Emergency stop (throttle command 0 -> disarm).
void kill();

// Last throttle percent written (for telemetry logs).
float last_throttle_pct();

}  // namespace esc_dshot
