#pragma once
// =============================================================================
//  thrust.h  -  Dual HX711 load-cell driver
// =============================================================================
//  Wraps two bogde/HX711 instances behind a simple API:
//      thrust::begin()          - init HX711s with default calibration
//      thrust::tare()           - zero both cells (no weight on them)
//      thrust::get_thrust()     - returns {cell1, cell2, total, asymmetry}
//      thrust::calibrate(g)     - runs a blocking CAL procedure
//
//  CALIBRATION PROCEDURE
//  ---------------------
//  1. Ensure NOTHING is touching the cells. Send "CAL" from receiver.py
//     or call thrust::calibrate(known_grams) directly.
//  2. Function takes a raw tare reading with zero load.
//  3. It then prints "place <known_grams>g on cell 1, send any byte" and
//     waits for the user. Repeat for cell 2.
//  4. Factor is computed as:
//
//         factor = (raw_with_weight - raw_tare) / known_grams
//
//     This matches the bogde/HX711 convention where grams = (raw - tare) / factor
//     so grams read back correctly.
//  5. Factors are stored in g_factor1 / g_factor2 and used on every read.
//     They are NOT persisted across reboots - edit config.h if you want
//     to hard-code them after a good calibration run.
// =============================================================================

#include <Arduino.h>

namespace thrust {

struct Reading {
    float cell1_grams;   // calibrated grams, cell 1
    float cell2_grams;   // calibrated grams, cell 2
    float total_grams;   // cell1 + cell2
    float asymmetry;     // (cell1 - cell2) - positive => left > right
};

// Initialise both HX711s. Must be called from setup().
void begin();

// Zero both cells. Call this with no load on the stand.
void tare();

// Non-blocking: returns the latest Reading using bogde's get_units().
// Internally averages `samples` conversions (default 1 for speed at 80 SPS).
Reading get_thrust(uint8_t samples = 1);

// Run the interactive calibration sequence. Prints prompts to Serial and
// waits for a byte on Serial between steps. `known_grams` is the mass you
// will place on each cell (use the same weight for both or call twice
// with different masses if cells differ). Updates the live factors.
void calibrate(float known_grams);

// Manually override calibration factors (used when receiver.py pushes
// new values, or at startup from config.h defaults).
void set_factors(float f1, float f2);

// Read back the currently active factors (for debugging / telemetry).
float get_factor1();
float get_factor2();

}  // namespace thrust
