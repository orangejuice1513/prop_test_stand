// =============================================================================
//  test_esc_dshot.cpp  -  Standalone DShot600 ESC bring-up sketch
// =============================================================================
//  Purpose: verify that the ESP32 can talk DShot600 to the APD ESC and spin
//  the motor, BEFORE wiring into the full UDP / telemetry pipeline.
//
//  Build with the `test_esc_dshot` PlatformIO environment:
//      pio run -e test_esc_dshot -t upload
//      pio device monitor
//
//  *** SAFETY ***
//  - REMOVE THE PROPELLER before the first spin-up.
//  - Secure the motor mount.
//  - Watchdog auto-zeros throttle after 5 s of Serial silence.
//
//  Serial commands (type + press enter):
//      a         -> (re)start disarm stream (throttle 0 for 3 s)
//      k         -> kill (DShot command 0 = motor stop / disarm)
//      0..100    -> throttle percent (maps to DShot 48..2047)
//      +  /  -   -> bump throttle by +/-5 %
//      h         -> help
// =============================================================================

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "config.h"
#include "dshot600_rmt.h"

static constexpr uint32_t WATCHDOG_MS = 5000;

static volatile uint16_t g_throttle    = 0;
static float             g_last_pct    = 0.0f;
static uint32_t          g_last_cmd_ms = 0;
static TaskHandle_t      g_task        = nullptr;
static String            g_line;

// ---------------------------------------------------------------------------
//  Sender task: pump DShot frames at 1000/DSHOT_PERIOD_MS Hz so the ESC
//  never starves for commands (most ESCs time out in ~100 ms of silence).
// ---------------------------------------------------------------------------
static void dshot_task(void*) {
    const TickType_t period = pdMS_TO_TICKS(DSHOT_PERIOD_MS);
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        dshot600::send(g_throttle, /*telemetry=*/false);
        vTaskDelayUntil(&last, period);
    }
}

static void set_pct(float pct) {
    if (pct < 0)                       pct = 0;
    if (pct > SAFETY_MAX_THROTTLE_PCT) pct = SAFETY_MAX_THROTTLE_PCT;

    uint16_t v;
    if (pct <= 0.0f) {
        v = DSHOT_MIN;     // 48 = "zero throttle, armed"
    } else {
        v = (uint16_t)(DSHOT_MIN +
                       (DSHOT_MAX - DSHOT_MIN) * (pct / 100.0f));
    }
    g_throttle    = v;
    g_last_pct    = pct;
    g_last_cmd_ms = millis();
    Serial.printf("[dshot] throttle=%.1f%%  dshot_raw=%u\n", pct, (unsigned)v);
}

static void disarm() {
    g_throttle    = 0;     // DShot command 0 = motor stop / disarm
    g_last_pct    = 0.0f;
    g_last_cmd_ms = millis();
    Serial.println("[dshot] disarmed (throttle command 0)");
}

static void arm_sequence() {
    Serial.println("[dshot] streaming throttle=0 for 3 s (arming)...");
    disarm();
    delay(3000);
    Serial.println("[dshot] armed - try '5' or '10' to start slow");
}

static void print_help() {
    Serial.println();
    Serial.println("=== ESC bring-up (DShot600) ===");
    Serial.printf("signal pin = GPIO%d\n", ESC_SIGNAL_PIN);
    Serial.printf("dshot range = %d..%d (0 = disarm)\n", DSHOT_MIN, DSHOT_MAX);
    Serial.printf("safety max throttle = %d %%\n", SAFETY_MAX_THROTTLE_PCT);
    Serial.println("Commands (type + <enter>):");
    Serial.println("  a         re-arm (disarm stream for 3 s)");
    Serial.println("  k         kill (command 0)");
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
    Serial.println("[test_esc_dshot] booting");
    Serial.println("[test_esc_dshot] *** CHECK: PROP REMOVED, MOTOR SECURED ***");

    if (!dshot600::begin(ESC_SIGNAL_PIN)) {
        Serial.println("[test_esc_dshot] FATAL: RMT init failed");
        while (true) delay(1000);
    }

    xTaskCreatePinnedToCore(dshot_task, "dshot",
                            2048, nullptr,
                            configMAX_PRIORITIES - 2,
                            &g_task, 0);

    arm_sequence();
    print_help();
}

static void handle_line() {
    g_line.trim();
    if (g_line.length() == 0) return;

    if      (g_line == "a") arm_sequence();
    else if (g_line == "k") disarm();
    else if (g_line == "h") print_help();
    else if (g_line == "+") set_pct(g_last_pct + 5.0f);
    else if (g_line == "-") set_pct(g_last_pct - 5.0f);
    else {
        float v = g_line.toFloat();
        bool looks_numeric = (g_line[0] >= '0' && g_line[0] <= '9');
        if (looks_numeric && v >= 0 && v <= 100) set_pct(v);
        else Serial.printf("[dshot] unknown: '%s'\n", g_line.c_str());
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
        Serial.println("[dshot] watchdog timeout -> throttle 0");
        set_pct(0);
    }
}
