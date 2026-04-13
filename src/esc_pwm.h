#pragma once
// =============================================================================
//  esc_pwm.h  -  Standard 50 Hz servo-PWM ESC driver
// =============================================================================
//  Uses the ESP32 LEDC peripheral (ledcWrite) to generate a 50 Hz square wave
//  whose HIGH time maps 1000 us (min throttle) .. 2000 us (full throttle).
//
//  Almost every hobby ESC (including the APD 120F3 V2 in PWM mode) accepts
//  this signal. Expect up to ~20 ms of latency (one PWM period) before the
//  ESC acts on a new command.
//
//  !!! SAFETY !!!
//  Always arm the ESC with min-throttle for at least 1-3 seconds before
//  allowing any other value. esc_pwm::begin() does this for you.
// =============================================================================

#include <Arduino.h>

namespace esc_pwm {

// Initialise the LEDC channel and run the arm sequence. Blocks for
// PWM_ARM_MS milliseconds while sending min throttle.
void begin();

// Write a microsecond pulse width directly (1000..2000). Clamped to range.
void write_us(uint16_t us);

// Write a throttle percent (0..100). Clamped to SAFETY_MAX_THROTTLE_PCT.
void write_throttle_pct(float pct);

// Emergency stop: pulse width = 1000 us (min throttle, motor off).
void kill();

// Last throttle percent written (for telemetry logs).
float last_throttle_pct();

}  // namespace esc_pwm
