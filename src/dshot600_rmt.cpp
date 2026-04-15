// =============================================================================
//  dshot600_rmt.cpp  -  Minimal DShot600 transmitter (legacy RMT API)
// =============================================================================
#include "dshot600_rmt.h"

#include <Arduino.h>
#include <driver/rmt.h>

namespace dshot600 {

// -- Timing in RMT ticks (12.5 ns each at clk_div = 1) -----------------------
static constexpr uint16_t T0H_TICKS = 50;   //  625 ns
static constexpr uint16_t T0L_TICKS = 83;   // 1042 ns  -> 1667 ns period
static constexpr uint16_t T1H_TICKS = 100;  // 1250 ns
static constexpr uint16_t T1L_TICKS = 33;   //  417 ns  -> 1667 ns period

static constexpr rmt_channel_t kChannel = RMT_CHANNEL_0;
static bool g_inited = false;

// ---------------------------------------------------------------------------
//  Standard DShot 16-bit frame: 11 throttle, 1 telemetry, 4 XOR checksum.
// ---------------------------------------------------------------------------
static uint16_t make_frame(uint16_t throttle_11bit, bool telemetry) {
    uint16_t packet = (uint16_t)((throttle_11bit & 0x07FF) << 1) |
                      (telemetry ? 1u : 0u);

    // Checksum = nibble-wise XOR of the 12-bit (packet) value.
    uint16_t csum = 0;
    uint16_t tmp  = packet;
    for (int i = 0; i < 3; i++) {
        csum ^= tmp;
        tmp  >>= 4;
    }
    csum &= 0x0F;

    return (uint16_t)((packet << 4) | csum);
}

// ---------------------------------------------------------------------------
bool begin(int gpio_pin) {
    if (g_inited) return true;

    rmt_config_t cfg = {};
    cfg.rmt_mode                       = RMT_MODE_TX;
    cfg.channel                        = kChannel;
    cfg.gpio_num                       = (gpio_num_t)gpio_pin;
    cfg.mem_block_num                  = 1;
    cfg.clk_div                        = 1;   // 80 MHz tick
    cfg.tx_config.loop_en              = false;
    cfg.tx_config.carrier_en           = false;
    cfg.tx_config.idle_output_en       = true;
    cfg.tx_config.idle_level           = RMT_IDLE_LEVEL_LOW;
    cfg.tx_config.carrier_freq_hz      = 38000;
    cfg.tx_config.carrier_duty_percent = 50;
    cfg.tx_config.carrier_level        = RMT_CARRIER_LEVEL_HIGH;

    if (rmt_config(&cfg) != ESP_OK) {
        Serial.println("[dshot600] rmt_config failed");
        return false;
    }
    if (rmt_driver_install(kChannel, 0, 0) != ESP_OK) {
        Serial.println("[dshot600] rmt_driver_install failed");
        return false;
    }

    g_inited = true;
    Serial.printf("[dshot600] RMT ch0 -> GPIO%d, 600 kbit/s\n", gpio_pin);
    return true;
}

// ---------------------------------------------------------------------------
void send(uint16_t throttle_11bit, bool telemetry) {
    if (!g_inited) return;

    const uint16_t frame = make_frame(throttle_11bit, telemetry);

    // 16 data bits + 1 terminator item.
    rmt_item32_t items[17];
    for (int i = 0; i < 16; i++) {
        const bool bit = (frame >> (15 - i)) & 0x1;
        items[i].level0    = 1;
        items[i].duration0 = bit ? T1H_TICKS : T0H_TICKS;
        items[i].level1    = 0;
        items[i].duration1 = bit ? T1L_TICKS : T0L_TICKS;
    }
    items[16].val = 0;  // end-of-transmission marker

    // wait_tx_done=true blocks until the frame is on the wire (~27 us).
    rmt_write_items(kChannel, items, 17, /*wait_tx_done=*/true);
}

}  // namespace dshot600
