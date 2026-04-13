// =============================================================================
//  thrust.cpp  -  Dual HX711 load-cell driver implementation
// =============================================================================
#include "thrust.h"

#include <HX711.h>

#include "config.h"

namespace thrust {

// Module-private state -------------------------------------------------------
static HX711 g_cell1;
static HX711 g_cell2;
static float g_factor1 = LC1_DEFAULT_FACTOR;
static float g_factor2 = LC2_DEFAULT_FACTOR;

// ---------------------------------------------------------------------------
void begin() {
    // The bogde library's begin(DOUT, SCK) wires pins and puts the HX711 in
    // default gain=128, channel A mode, which is what we want for both cells.
    g_cell1.begin(LC1_DOUT, LC1_SCK);
    g_cell2.begin(LC2_DOUT, LC2_SCK);

    // HX711 needs a little wake-up time after power-on.
    delay(50);

    // Apply default scale factors. set_scale() divides raw counts by factor
    // when you call get_units(), so units = (raw - offset) / factor.
    g_cell1.set_scale(g_factor1);
    g_cell2.set_scale(g_factor2);

    // Wait until both ADCs report ready at least once before taring.
    uint32_t t0 = millis();
    while ((!g_cell1.is_ready() || !g_cell2.is_ready()) && millis() - t0 < 1000) {
        delay(5);
    }

    tare();
    Serial.printf("[thrust] init done. factors: %.3f / %.3f\n", g_factor1, g_factor2);
}

// ---------------------------------------------------------------------------
void tare() {
    // Average 10 readings to get a stable zero-load offset.
    if (g_cell1.wait_ready_timeout(500)) g_cell1.tare(10);
    if (g_cell2.wait_ready_timeout(500)) g_cell2.tare(10);
    Serial.println("[thrust] tared");
}

// ---------------------------------------------------------------------------
Reading get_thrust(uint8_t samples) {
    Reading r{0, 0, 0, 0};

    // get_units() blocks until `samples` conversions are available. At the
    // default 10 SPS that's 100 ms per sample, so we keep samples=1 on the
    // fast telemetry path and use samples=10 only during calibration.
    if (g_cell1.wait_ready_timeout(20)) {
        r.cell1_grams = g_cell1.get_units(samples);
    }
    if (g_cell2.wait_ready_timeout(20)) {
        r.cell2_grams = g_cell2.get_units(samples);
    }

    r.total_grams = r.cell1_grams + r.cell2_grams;
    r.asymmetry   = r.cell1_grams - r.cell2_grams;
    return r;
}

// ---------------------------------------------------------------------------
//  Interactive calibration.
//  Blocks on Serial input between steps. Send ANY byte from the monitor
//  (or receiver.py) to advance each step.
// ---------------------------------------------------------------------------
static void drain_serial() {
    while (Serial.available()) Serial.read();
}

static void wait_for_any_byte(const char* prompt) {
    drain_serial();
    Serial.println(prompt);
    while (!Serial.available()) delay(20);
    drain_serial();
}

static long read_raw_average(HX711& cell, uint8_t n = 20) {
    // read_average() gives us the un-scaled, un-offset ADC counts averaged
    // over n samples. This is exactly what we want for calibration math.
    return cell.wait_ready_timeout(1000) ? cell.read_average(n) : 0;
}

void calibrate(float known_grams) {
    Serial.println("[thrust] === CALIBRATION START ===");
    Serial.printf("[thrust] known weight = %.2f g\n", known_grams);

    // -- Step 1: reset scale to 1 so read_average() returns raw counts.
    g_cell1.set_scale();
    g_cell2.set_scale();
    g_cell1.set_offset(0);
    g_cell2.set_offset(0);

    // -- Step 2: zero-load tare.
    wait_for_any_byte("Remove ALL weight from both cells, then send any key...");
    long tare1 = read_raw_average(g_cell1);
    long tare2 = read_raw_average(g_cell2);
    Serial.printf("[thrust] raw tare: cell1=%ld  cell2=%ld\n", tare1, tare2);

    // -- Step 3: known weight on cell 1.
    char buf[80];
    snprintf(buf, sizeof(buf),
             "Place %.1f g on CELL 1 only, then send any key...", known_grams);
    wait_for_any_byte(buf);
    long raw1 = read_raw_average(g_cell1);
    float f1 = (float)(raw1 - tare1) / known_grams;
    Serial.printf("[thrust] raw1=%ld  factor1=%.3f\n", raw1, f1);

    // -- Step 4: known weight on cell 2.
    snprintf(buf, sizeof(buf),
             "Place %.1f g on CELL 2 only, then send any key...", known_grams);
    wait_for_any_byte(buf);
    long raw2 = read_raw_average(g_cell2);
    float f2 = (float)(raw2 - tare2) / known_grams;
    Serial.printf("[thrust] raw2=%ld  factor2=%.3f\n", raw2, f2);

    // -- Step 5: commit factors & offsets.
    g_factor1 = f1;
    g_factor2 = f2;
    g_cell1.set_scale(g_factor1);
    g_cell2.set_scale(g_factor2);
    g_cell1.set_offset(tare1);
    g_cell2.set_offset(tare2);

    Serial.println("[thrust] === CALIBRATION DONE ===");
    Serial.printf("[thrust] factors: %.3f / %.3f\n", g_factor1, g_factor2);
    Serial.println("[thrust] copy these into config.h to persist across reboots");
}

// ---------------------------------------------------------------------------
void set_factors(float f1, float f2) {
    g_factor1 = f1;
    g_factor2 = f2;
    g_cell1.set_scale(g_factor1);
    g_cell2.set_scale(g_factor2);
}

float get_factor1() { return g_factor1; }
float get_factor2() { return g_factor2; }

}  // namespace thrust
