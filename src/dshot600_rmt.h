#pragma once
// =============================================================================
//  dshot600_rmt.h  -  Minimal DShot600 transmitter using the ESP32 RMT
//                     peripheral (legacy driver/rmt.h API).
// =============================================================================
//  Replaces the derdoktor667/DShotRMT library, which requires the IDF 5.x
//  RMT encoder API (driver/rmt_tx.h). Our Arduino-ESP32 2.0.x toolchain only
//  ships the legacy driver, so we talk to the peripheral directly.
//
//  DShot600 timing (80 MHz APB clock, clock divider = 1, one tick = 12.5 ns):
//      bit period : 1667 ns = 133 ticks
//      T0H / T0L  :  625 ns /1042 ns = 50 / 83 ticks
//      T1H / T1L  : 1250 ns / 417 ns =100 / 33 ticks
//
//  Frame layout (16 bits, MSB first):
//      [11 bits throttle][1 bit telemetry-request][4 bits XOR checksum]
//
//  Throttle 0       = motor stop / disarm command
//  Throttle 1..47   = reserved commands (beep, save settings, 3D, ...)
//  Throttle 48..2047 = actual throttle range (0..100 %)
// =============================================================================

#include <stdint.h>

namespace dshot600 {

// Initialise RMT channel 0 on the given GPIO. Safe to call more than once -
// subsequent calls are no-ops. Returns true on success.
bool begin(int gpio_pin);

// Transmit one DShot frame (blocks ~27 us until the RMT finishes).
//   throttle_11bit  - 0..2047 (0 = disarm, 48..2047 = throttle range)
//   telemetry       - set the telemetry-request bit (unused in this project)
void send(uint16_t throttle_11bit, bool telemetry = false);

}  // namespace dshot600
