// =============================================================================
//  test_esc.cpp  -  Standalone 50 Hz servo-PWM ESC bring-up sketch
// =============================================================================
//  Purpose: verify that the ESP32 can arm the APD ESC and spin the motor over
//  the signal pin, BEFORE integrating with the full UDP telemetry pipeline.
//
//  Build with the `test_esc` PlatformIO environment:
//      pio run -e test_esc -t upload
//      pio device monitor
//
//  *** SAFETY ***
//  - REMOVE THE PROPELLER before the first spin-up.
//  - Secure the motor mount. A hand-held motor will jump.
//  - Keep the battery plugged in only while testing; disconnect before
//    touching the wiring.
//  - A 5 s command-watchdog auto-zeros throttle if the Serial monitor goes
//    quiet. It is not a substitute for a hardware kill.
//
//  Serial commands (type + press enter in the PlatformIO monitor):
//      a         -> re-run arm sequence (1000 us for PWM_ARM_MS)
//      k         -> kill (throttle 0, 1000 us)
//      0..100    -> throttle percent
//      +  /  -   -> bump throttle by +/-5 %
//      h         -> help
// =============================================================================

#include <Arduino.h>

#include "config.h"

static constexpr uint32_t PWM_PERIOD_US = 1000000UL / PWM_FREQ_HZ;
static constexpr uint32_t PWM_MAX_DUTY  = (1UL << PWM_RESOLUTION) - 1;
static constexpr uint32_t WATCHDOG_MS   = 5000;

static float    g_last_pct    = 0.0f;
static uint32_t g_last_cmd_ms = 0;
static String   g_line;

static uint32_t us_to_duty(uint32_t us) {
    if (us < PWM_MIN_US) us = PWM_MIN_US;
    if (us > PWM_MAX_US) us = PWM_MAX_US;
    return (uint32_t)((uint64_t)us * (PWM_MAX_DUTY + 1) / PWM_PERIOD_US);
}

static void write_pct(float pct) {
    if (pct < 0)                       pct = 0;
    if (pct > SAFETY_MAX_THROTTLE_PCT) pct = SAFETY_MAX_THROTTLE_PCT;
    uint32_t us = PWM_MIN_US + (uint32_t)((PWM_MAX_US - PWM_MIN_US) * (pct / 100.0f));
    ledcWrite(PWM_CHANNEL, us_to_duty(us));
    g_last_pct    = pct;
    g_last_cmd_ms = millis();
    Serial.printf("[esc] throttle=%.1f%%  pulse=%u us\n", pct, (unsigned)us);
}

static void arm_sequence() {
    Serial.printf("[esc] arming: %u us for %u ms ...\n", PWM_MIN_US, PWM_ARM_MS);
    ledcWrite(PWM_CHANNEL, us_to_duty(PWM_MIN_US));
    delay(PWM_ARM_MS);
    g_last_pct    = 0.0f;
    g_last_cmd_ms = millis();
    Serial.println("[esc] armed - listen for ESC confirmation beeps");
    Serial.println("[esc] try '5' or '10' first to confirm direction");
}

static void print_help() {
    Serial.println();
    Serial.println("=== ESC bring-up ===");
    Serial.printf("signal pin = GPIO%d, pulse range %u..%u us @ %u Hz\n",
                  ESC_SIGNAL_PIN, PWM_MIN_US, PWM_MAX_US, PWM_FREQ_HZ);
    Serial.printf("safety max throttle = %d %%\n", SAFETY_MAX_THROTTLE_PCT);
    Serial.println("Commands (type + <enter>):");
    Serial.println("  a         re-arm");
    Serial.println("  k         kill (1000 us)");
    Serial.println("  0..100    throttle percent");
    Serial.println("  +/-       bump throttle by 5 %");
    Serial.println("  h         help");
    Serial.printf("Watchdog: throttle auto-zeros after %u ms of silence.\n",
                  (unsigned)WATCHDOG_MS);
    Serial.println();
}

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println();
    Serial.println("[test_esc] booting");
    Serial.println("[test_esc] *** CHECK: PROP REMOVED, MOTOR SECURED ***");

    ledcSetup(PWM_CHANNEL, PWM_FREQ_HZ, PWM_RESOLUTION);
    ledcAttachPin(ESC_SIGNAL_PIN, PWM_CHANNEL);

    arm_sequence();
    print_help();
}

static void handle_line() {
    g_line.trim();
    if (g_line.length() == 0) return;

    if      (g_line == "a") arm_sequence();
    else if (g_line == "k") { write_pct(0); Serial.println("[esc] killed"); }
    else if (g_line == "h") print_help();
    else if (g_line == "+") write_pct(g_last_pct + 5.0f);
    else if (g_line == "-") write_pct(g_last_pct - 5.0f);
    else {
        float v = g_line.toFloat();
        // toFloat() returns 0 on parse failure, so guard with an explicit check
        // for a leading digit to distinguish a real "0" from garbage.
        bool looks_numeric = (g_line[0] >= '0' && g_line[0] <= '9');
        if (looks_numeric && v >= 0 && v <= 100) write_pct(v);
        else Serial.printf("[esc] unknown: '%s'\n", g_line.c_str());
    }
}

void loop() {
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\r') continue;
        if (c == '\n') { handle_line(); g_line = ""; }
        else            g_line += c;
    }

    if (millis() - g_last_cmd_ms > WATCHDOG_MS && g_last_pct > 0.0f) {
        Serial.println("[esc] watchdog timeout -> throttle 0");
        write_pct(0);
    }
}
