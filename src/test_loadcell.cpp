// =============================================================================
//  test_loadcell.cpp  -  Standalone HX711 bring-up sketch
// =============================================================================
//  Purpose: verify both load cells are wired correctly and read sane values
//  BEFORE integrating with the full telemetry / WiFi / ESC pipeline.
//
//  Build with the `test_loadcell` PlatformIO environment:
//      pio run -e test_loadcell -t upload
//      pio device monitor
//
//  Serial commands (type the character in the monitor and hit enter):
//      t  -> tare both cells (zero them out)
//      r  -> toggle raw-counts mode (no scaling, no offset)
//      h  -> print help
// =============================================================================

#include <Arduino.h>
#include <HX711.h>

#include "config.h"

static HX711 cell1;
static HX711 cell2;
static bool  g_raw_mode  = false;
static uint32_t g_last_print = 0;

static void print_help() {
    Serial.println();
    Serial.println("=== Load cell bring-up ===");
    Serial.printf("cell1: DT=GPIO%d  SCK=GPIO%d\n", LC1_DOUT, LC1_SCK);
    Serial.printf("cell2: DT=GPIO%d  SCK=GPIO%d\n", LC2_DOUT, LC2_SCK);
    Serial.printf("factors (from config.h): cell1=%.3f  cell2=%.3f\n",
                  LC1_DEFAULT_FACTOR, LC2_DEFAULT_FACTOR);
    Serial.println("Commands:");
    Serial.println("  t  -> tare both cells");
    Serial.println("  r  -> toggle raw-counts mode");
    Serial.println("  h  -> help");
    Serial.println("Streaming at ~5 Hz. Press/release the cell with your finger");
    Serial.println("to confirm the sign and magnitude look reasonable.");
    Serial.println();
}

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println();
    Serial.println("[test_loadcell] booting");

    cell1.begin(LC1_DOUT, LC1_SCK);
    cell2.begin(LC2_DOUT, LC2_SCK);
    delay(50);

    cell1.set_scale(LC1_DEFAULT_FACTOR);
    cell2.set_scale(LC2_DEFAULT_FACTOR);

    // Wait up to 2 s for both HX711s to produce their first conversion.
    uint32_t t0 = millis();
    while ((!cell1.is_ready() || !cell2.is_ready()) && millis() - t0 < 2000) {
        delay(5);
    }
    if (!cell1.is_ready())
        Serial.println("[WARN] cell1 never became ready - check DT/SCK/VCC wiring");
    if (!cell2.is_ready())
        Serial.println("[WARN] cell2 never became ready - check DT/SCK/VCC wiring");

    if (cell1.wait_ready_timeout(500)) cell1.tare(10);
    if (cell2.wait_ready_timeout(500)) cell2.tare(10);

    print_help();
}

void loop() {
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\r' || c == '\n') continue;
        switch (c) {
            case 't':
                Serial.println("[cmd] taring...");
                if (cell1.wait_ready_timeout(500)) cell1.tare(10);
                if (cell2.wait_ready_timeout(500)) cell2.tare(10);
                Serial.println("[cmd] tared");
                break;
            case 'r':
                g_raw_mode = !g_raw_mode;
                Serial.printf("[cmd] raw mode = %s\n", g_raw_mode ? "ON" : "OFF");
                break;
            case 'h':
                print_help();
                break;
            default:
                break;
        }
    }

    if (millis() - g_last_print >= 200) {  // ~5 Hz
        g_last_print = millis();

        if (g_raw_mode) {
            long r1 = cell1.is_ready() ? cell1.read() : 0;
            long r2 = cell2.is_ready() ? cell2.read() : 0;
            Serial.printf("raw1=%11ld   raw2=%11ld\n", r1, r2);
        } else {
            float g1 = cell1.wait_ready_timeout(20) ? cell1.get_units(1) : 0.0f;
            float g2 = cell2.wait_ready_timeout(20) ? cell2.get_units(1) : 0.0f;
            Serial.printf("cell1=%9.2f g    cell2=%9.2f g    total=%9.2f g\n",
                          g1, g2, g1 + g2);
        }
    }
}
