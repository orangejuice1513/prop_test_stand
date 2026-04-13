// =============================================================================
//  esc_dshot.cpp  -  DShot600 driver (derdoktor667/DShotRMT + FreeRTOS task)
// =============================================================================
#include "esc_dshot.h"

#include "config.h"

// Only compile this translation unit when the user opts into DShot via the
// platformio.ini build flag. The PWM build links the empty stubs at the
// bottom of this file so main.cpp's esc_dshot:: calls still resolve.
#ifdef USE_DSHOT

#include <DShotRMT.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace esc_dshot {

// ---------------------------------------------------------------------------
//  Library instance.
//
//  derdoktor667/DShotRMT v0.9.5 constructor:
//      DShotRMT(gpio_num_t gpio,
//               dshot_mode_t mode              = DSHOT300,
//               bool         is_bidirectional  = false,
//               uint16_t     magnet_count      = DEFAULT_MOTOR_MAGNET_COUNT);
//
//  We hard-code DSHOT600, unidirectional=false, and leave magnet_count at
//  the library default (14) because we don't consume the RPM field.
// ---------------------------------------------------------------------------
static DShotRMT g_dshot((gpio_num_t)ESC_SIGNAL_PIN,
                        DSHOT600,
                        /*is_bidirectional=*/false);

static volatile uint16_t g_throttle = 0;        // latest value to transmit
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
        // sendThrottle() accepts raw 11-bit throttle values. 0 is the
        // reserved "motor stop / disarm" command.
        g_dshot.sendThrottle(g_throttle);
        vTaskDelayUntil(&last, period);
    }
}

// ---------------------------------------------------------------------------
void begin() {
    // Library init. Returns a dshot_result_t but we don't inspect it here
    // beyond logging - most failures are unrecoverable anyway.
    g_dshot.begin();

    // Throttle 0 = disarm. Sender task will transmit this while the ESC
    // runs its boot beeps.
    g_throttle = 0;
    g_last_pct = 0.0f;

    xTaskCreatePinnedToCore(dshot_task,
                            "dshot",
                            2048,
                            nullptr,
                            configMAX_PRIORITIES - 2,   // high priority
                            &g_task,
                            0);                         // pin to Core 0

    Serial.println("[esc_dshot] DSHOT600 sender running on core 0 @ 500 Hz");
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

    // Track percent for logs.
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
float    last_throttle_pct()        { return 0.f; }
}

#endif  // USE_DSHOT
