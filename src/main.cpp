// =============================================================================
//  Propeller Test Stand - main.cpp
// =============================================================================
//
//  Hardware:
//      ESP32 DevKit v1
//      2x HX711 load cells (DT=GPIO4/17, SCK=GPIO16/18)
//      APD 120F3 V2 ESC: signal = GPIO19, telemetry = GPIO13 (UART2 RX)
//
//  Protocols:
//      The ESC signal line uses 50 Hz servo PWM by default. Define USE_DSHOT
//      at compile time (see platformio.ini) to switch to DShot600.
//
// -----------------------------------------------------------------------------
//                     !!!!!!  SAFETY WARNINGS  !!!!!!
// -----------------------------------------------------------------------------
//  You are spinning a high-power brushless motor with a rigid propeller.
//
//   * ALWAYS bolt the stand to a heavy fixture before powering on.
//   * ALWAYS remove the prop before first bench tests.
//   * ALWAYS stand OUT OF PLANE of the prop disc. Blades that let go go
//     straight out at thousands of RPM.
//   * Wear eye and hearing protection. A 6S setup at 100% can exceed 110 dB.
//   * Keep a physical E-stop (battery disconnect) within arm's reach.
//   * The software KILL command is a courtesy - do not rely on it as your
//     only safety device. WiFi dies. Code crashes. Batteries don't.
//   * Cross-check the calibration factor before trusting thrust numbers.
//   * After flashing new firmware, power-cycle the ESC to re-arm cleanly.
// =============================================================================

#include <Arduino.h>

#include "config.h"
#include "esc_dshot.h"
#include "esc_pwm.h"
#include "esc_telem.h"
#include "telemetry.h"
#include "thrust.h"

// -----------------------------------------------------------------------------
//  ESC protocol selector
// -----------------------------------------------------------------------------
//  One helper layer that resolves to either esc_pwm:: or esc_dshot:: based on
//  the USE_DSHOT compile flag. This keeps loop() free of #ifdef spaghetti.
// -----------------------------------------------------------------------------
namespace esc {
    static inline void  begin()                        {
#ifdef USE_DSHOT
        esc_dshot::begin();
#else
        esc_pwm::begin();
#endif
    }
    static inline void  write_throttle_pct(float pct)  {
#ifdef USE_DSHOT
        esc_dshot::write_throttle_pct(pct);
#else
        esc_pwm::write_throttle_pct(pct);
#endif
    }
    static inline void  kill()                         {
#ifdef USE_DSHOT
        esc_dshot::kill();
#else
        esc_pwm::kill();
#endif
    }
    static inline float last_throttle_pct()            {
#ifdef USE_DSHOT
        return esc_dshot::last_throttle_pct();
#else
        return esc_pwm::last_throttle_pct();
#endif
    }
}

// -----------------------------------------------------------------------------
//  Simple state machine
// -----------------------------------------------------------------------------
enum class State { BOOT, DISARMED, ARMED, RAMPING, CALIBRATING };
static State g_state = State::BOOT;

// Ramp test parameters (used by the RAMP command).
struct Ramp {
    uint32_t start_ms;
    uint32_t step_ms  = 1500;    // dwell at each throttle step
    float    pct_min  = 0.0f;
    float    pct_max  = 80.0f;
    float    pct_step = 10.0f;
    float    pct_cur  = 0.0f;
    bool     descending = false;
};
static Ramp g_ramp;

// -----------------------------------------------------------------------------
//  Helper: build a telemetry::Sample from all sensor sources.
// -----------------------------------------------------------------------------
static telemetry::Sample collect_sample() {
    thrust::Reading  t = thrust::get_thrust(1);
    esc_telem::Frame e = esc_telem::latest();

    telemetry::Sample s{};
    s.cell1_g      = t.cell1_grams;
    s.cell2_g      = t.cell2_grams;
    s.total_g      = t.total_grams;
    s.diff_g       = t.asymmetry;
    s.throttle_pct = esc::last_throttle_pct();
    s.rpm          = e.erpm;
    s.voltage_v    = e.voltage_v;
    s.current_a    = e.current_a;
    s.temp_c       = e.mos_temp_c;
    return s;
}

// -----------------------------------------------------------------------------
//  Command dispatcher
// -----------------------------------------------------------------------------
static void handle_event(const telemetry::Event& ev) {
    switch (ev.cmd) {
        case telemetry::Command::NONE: return;

        case telemetry::Command::ARM:
            Serial.println("[main] cmd ARM");
            esc::begin();                 // safe to call again; runs arm seq
            g_state = State::ARMED;
            break;

        case telemetry::Command::KILL:
            Serial.println("[main] cmd KILL");
            esc::kill();
            g_state = State::DISARMED;
            break;

        case telemetry::Command::TARE:
            Serial.println("[main] cmd TARE");
            thrust::tare();
            break;

        case telemetry::Command::THROTTLE:
            Serial.printf("[main] cmd T%.0f\n", ev.value);
            if (g_state != State::ARMED && g_state != State::RAMPING) {
                Serial.println("[main] ignored: not armed");
                break;
            }
            esc::write_throttle_pct(ev.value);
            break;

        case telemetry::Command::CALIBRATE:
            Serial.println("[main] cmd CAL -> entering blocking calibration");
            esc::kill();
            g_state = State::CALIBRATING;
            thrust::calibrate(1000.0f);  // 1 kg reference weight - edit as needed
            g_state = State::DISARMED;
            break;

        case telemetry::Command::RAMP:
            Serial.println("[main] cmd RAMP -> starting automated sweep");
            if (g_state != State::ARMED) {
                Serial.println("[main] ignored: not armed");
                break;
            }
            g_ramp.start_ms   = millis();
            g_ramp.pct_cur    = g_ramp.pct_min;
            g_ramp.descending = false;
            esc::write_throttle_pct(g_ramp.pct_cur);
            g_state = State::RAMPING;
            break;
    }
}

// -----------------------------------------------------------------------------
//  Ramp tick - called once per main loop while g_state==RAMPING.
// -----------------------------------------------------------------------------
static void ramp_tick() {
    if (millis() - g_ramp.start_ms < g_ramp.step_ms) return;
    g_ramp.start_ms = millis();

    if (!g_ramp.descending) {
        g_ramp.pct_cur += g_ramp.pct_step;
        if (g_ramp.pct_cur >= g_ramp.pct_max) {
            g_ramp.pct_cur    = g_ramp.pct_max;
            g_ramp.descending = true;
        }
    } else {
        g_ramp.pct_cur -= g_ramp.pct_step;
        if (g_ramp.pct_cur <= g_ramp.pct_min) {
            g_ramp.pct_cur = g_ramp.pct_min;
            esc::write_throttle_pct(0);
            g_state = State::ARMED;
            Serial.println("[main] ramp done");
            return;
        }
    }
    esc::write_throttle_pct(g_ramp.pct_cur);
    Serial.printf("[main] ramp -> %.1f%%\n", g_ramp.pct_cur);
}

// =============================================================================
//  Arduino entry points
// =============================================================================
void setup() {
    Serial.begin(115200);
    delay(250);
    Serial.println();
    Serial.println("========================================");
    Serial.println(" Prop Test Stand booting");
#ifdef USE_DSHOT
    Serial.println(" ESC protocol: DShot600");
#else
    Serial.println(" ESC protocol: 50 Hz PWM");
#endif
    Serial.println("========================================");

    thrust::begin();
    esc_telem::begin();
    telemetry::begin();

    // Do NOT auto-arm the ESC. Wait for an explicit ARM command from the
    // receiver. This is intentional: a rebooting ESP32 should never start
    // spinning the prop on its own.
    g_state = State::DISARMED;

    Serial.println("[main] READY - send ARM when you're ready to go hot");
}

void loop() {
    // Pull ESC telemetry bytes out of UART2 every pass.
    esc_telem::update();

    // Check for incoming UDP command.
    telemetry::Event ev = telemetry::poll_command();
    handle_event(ev);

    // Deadman watchdog: if we're armed and haven't heard from the laptop
    // for COMMAND_TIMEOUT_MS, spin back down to idle throttle.
    if ((g_state == State::ARMED || g_state == State::RAMPING) &&
        telemetry::ms_since_last_command() > COMMAND_TIMEOUT_MS) {
        Serial.println("[main] watchdog: command timeout -> throttle 0");
        esc::write_throttle_pct(0);
    }

    if (g_state == State::RAMPING) ramp_tick();

    // Always stream telemetry, regardless of state. maybe_send() rate-limits.
    telemetry::Sample s = collect_sample();
    telemetry::maybe_send(s);
}
