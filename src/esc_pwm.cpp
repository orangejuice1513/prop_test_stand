// =============================================================================
//  esc_pwm.cpp  -  LEDC-based 50 Hz servo PWM ESC driver
// =============================================================================
#include "esc_pwm.h"

#include "config.h"

namespace esc_pwm {

// ---------------------------------------------------------------------------
//  LEDC math
//  ---------------------------------------------------------------------------
//  Period at 50 Hz  = 20 000 us
//  With 16-bit res  = 65536 duty steps
//  -> 1 us pulse    ≈ 65536 / 20000 ≈ 3.2768 duty counts
//
//  For a 1000 us pulse -> 3277 counts.
//  For a 2000 us pulse -> 6554 counts.
// ---------------------------------------------------------------------------
static constexpr uint32_t PWM_PERIOD_US = 1000000UL / PWM_FREQ_HZ; // 20 000
static constexpr uint32_t PWM_MAX_DUTY  = (1UL << PWM_RESOLUTION) - 1;

static float g_last_pct = 0.0f;

static uint32_t us_to_duty(uint32_t us) {
    if (us < PWM_MIN_US) us = PWM_MIN_US;
    if (us > PWM_MAX_US) us = PWM_MAX_US;
    // Scale us -> duty counts linearly over the whole period.
    return (uint32_t)((uint64_t)us * (PWM_MAX_DUTY + 1) / PWM_PERIOD_US);
}

// ---------------------------------------------------------------------------
void begin() {
    // Configure the LEDC channel at 50 Hz, 16-bit resolution, then attach it
    // to the signal pin. The ESP32 Arduino core takes care of the timer.
    ledcSetup(PWM_CHANNEL, PWM_FREQ_HZ, PWM_RESOLUTION);
    ledcAttachPin(ESC_SIGNAL_PIN, PWM_CHANNEL);

    // --- ARM sequence ---------------------------------------------------
    // Hold minimum throttle for PWM_ARM_MS (default 3000 ms). ESCs use this
    // quiet period to detect the 1 ms floor and enable the output stage.
    Serial.printf("[esc_pwm] arming (min throttle %u us for %u ms)...\n",
                  PWM_MIN_US, PWM_ARM_MS);
    ledcWrite(PWM_CHANNEL, us_to_duty(PWM_MIN_US));
    delay(PWM_ARM_MS);
    g_last_pct = 0.0f;
    Serial.println("[esc_pwm] armed");
}

// ---------------------------------------------------------------------------
void write_us(uint16_t us) {
    ledcWrite(PWM_CHANNEL, us_to_duty(us));
    // Track percent for telemetry.
    float pct = 100.0f * (float)(us - PWM_MIN_US) / (float)(PWM_MAX_US - PWM_MIN_US);
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;
    g_last_pct = pct;
}

// ---------------------------------------------------------------------------
void write_throttle_pct(float pct) {
    if (pct < 0)                          pct = 0;
    if (pct > SAFETY_MAX_THROTTLE_PCT)    pct = SAFETY_MAX_THROTTLE_PCT;

    uint32_t us = PWM_MIN_US + (uint32_t)((PWM_MAX_US - PWM_MIN_US) * (pct / 100.0f));
    write_us((uint16_t)us);
    g_last_pct = pct;
}

// ---------------------------------------------------------------------------
void kill() {
    ledcWrite(PWM_CHANNEL, us_to_duty(PWM_MIN_US));
    g_last_pct = 0.0f;
}

float last_throttle_pct() { return g_last_pct; }

}  // namespace esc_pwm
