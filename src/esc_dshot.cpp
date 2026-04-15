// =============================================================================
//  esc_dshot.cpp  -  DShot600 ESC driver on top of dshot600_rmt
// =============================================================================
//  Previously used derdoktor667/DShotRMT, but that library needs the IDF 5.x
//  RMT encoder headers which our Arduino-ESP32 2.0.x toolchain does not ship.
//  We now drive the RMT peripheral directly via src/dshot600_rmt.cpp.
// =============================================================================
#include "esc_dshot.h"

#include "config.h"

// Only compile this translation unit when the user opts into DShot via the
// platformio.ini build flag. The PWM build links the empty stubs at the
// bottom of this file so main.cpp's esc_dshot:: calls still resolve.
#ifdef USE_DSHOT

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "dshot600_rmt.h"

namespace esc_dshot {

static volatile uint16_t g_throttle = 0;  // 0..2047, 0 = disarm
static volatile float    g_last_pct = 0.0f;
static TaskHandle_t      g_task     = nullptr;

// ---------------------------------------------------------------------------
//  Sender task - pinned to Core 0. Arduino's loop() runs on Core 1, so
//  keeping the high-rate DShot stream on Core 0 avoids jitter from WiFi
//  or HX711 reads on the other core.
// ---------------------------------------------------------------------------
static void dshot_task(void*) {
    const TickType_t period = pdMS_TO_TICKS(DSHOT_PERIOD_MS);
    TickType_t last = xTaskGetTickCount();

    for (;;) {
        dshot600::send(g_throttle, /*telemetry=*/false);
        vTaskDelayUntil(&last, period);
    }
}

// ---------------------------------------------------------------------------
void begin() {
    if (!dshot600::begin(ESC_SIGNAL_PIN)) {
        Serial.println("[esc_dshot] RMT init failed - aborting");
        return;
    }

    // Throttle 0 = disarm. Sender task will transmit this while the ESC
    // runs its boot beeps.
    g_throttle = 0;
    g_last_pct = 0.0f;

    if (g_task == nullptr) {
        xTaskCreatePinnedToCore(dshot_task,
                                "dshot",
                                2048,
                                nullptr,
                                configMAX_PRIORITIES - 2,
                                &g_task,
                                0);
    }

    Serial.printf("[esc_dshot] DSHOT600 sender running on core 0 @ %u Hz\n",
                  (unsigned)(1000 / DSHOT_PERIOD_MS));
    Serial.println("[esc_dshot] arming (throttle=0 for 3 s)...");
    delay(3000);
    Serial.println("[esc_dshot] armed");
}

// ---------------------------------------------------------------------------
void write_raw(uint16_t v) {
    if (v != 0) {
        if (v < DSHOT_MIN) v = DSHOT_MIN;
        if (v > DSHOT_MAX) v = DSHOT_MAX;
    }
    g_throttle = v;

    if (v == 0) {
        g_last_pct = 0.0f;
    } else {
        g_last_pct = 100.0f * (float)(v - DSHOT_MIN) /
                     (float)(DSHOT_MAX - DSHOT_MIN);
    }
}

void write_throttle_pct(float pct) {
    if (pct <= 0) {
        write_raw(DSHOT_MIN);
        g_last_pct = 0.0f;
        return;
    }
    if (pct > SAFETY_MAX_THROTTLE_PCT) pct = SAFETY_MAX_THROTTLE_PCT;
    uint16_t v = DSHOT_MIN +
                 (uint16_t)((DSHOT_MAX - DSHOT_MIN) * (pct / 100.0f));
    write_raw(v);
    g_last_pct = pct;
}

void kill() {
    // Throttle 0 is the DShot "motor stop / disarm" command.
    g_throttle = 0;
    g_last_pct = 0.0f;
}

float last_throttle_pct() { return g_last_pct; }

}  // namespace esc_dshot

#else   // !USE_DSHOT - provide empty symbols so the linker is happy.

namespace esc_dshot {
void     begin()                   {}
void     write_raw(uint16_t)       {}
void     write_throttle_pct(float) {}
void     kill()                    {}
float    last_throttle_pct()       { return 0.f; }
}

#endif  // USE_DSHOT
