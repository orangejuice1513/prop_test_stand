#pragma once
// =============================================================================
//  esc_telem.h  -  APD 120F3 V2 ESC telemetry parser (UART2)
// =============================================================================
//  The APD F-series ESCs continuously broadcast a telemetry packet on
//  their "T" wire at 115200 baud, 8N1, 3.3 V logic. Each packet is 22
//  bytes and contains voltage, currents, RPM, duty cycles and three
//  temperatures. The last two bytes are a CRC16.
//
//  Packet layout (little-endian, based on APD's public doc):
//      [0..1]   RX throttle              u16
//      [2..3]   Output throttle          u16
//      [4..5]   RPM (electrical)         u16  * 100   -> eRPM
//      [6..7]   Input voltage            u16  * 0.01  -> V
//      [8..9]   Bus current              u16  * 0.01  -> A  (signed on newer FW)
//      [10..11] Phase-wire current       u16  * 0.01  -> A
//      [12..13] MOSFET temperature       u16  * 0.01  -> degC
//      [14..15] Capacitor temperature    u16  * 0.01  -> degC
//      [16..17] MCU temperature          u16  * 0.01  -> degC
//      [18..19] Reserved / status
//      [20..21] CRC16 (XMODEM poly 0x1021)
//
//  NOTE: APD has rev'd the packet a few times. If your numbers look
//  crazy, dump Serial2.read() bytes and compare to the newest APD doc.
// =============================================================================

#include <Arduino.h>

namespace esc_telem {

struct Frame {
    uint16_t rx_throttle;
    uint16_t out_throttle;
    uint32_t erpm;          // stored as u32 after *100 rescale
    float    voltage_v;
    float    current_a;
    float    phase_current_a;
    float    mos_temp_c;
    float    cap_temp_c;
    float    mcu_temp_c;
    uint32_t timestamp_ms;  // millis() at decode time
    bool     valid;
};

// Initialise Serial2 on the pin defined in config.h.
void begin();

// Pull any waiting bytes from the UART and try to decode packets.
// Call this frequently from loop() (or a task). Non-blocking.
void update();

// Return the most recently decoded frame (may be stale - check timestamp).
Frame latest();

}  // namespace esc_telem
