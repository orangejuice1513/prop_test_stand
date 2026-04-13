// =============================================================================
//  esc_telem.cpp  -  APD 120F3 V2 telemetry parser
// =============================================================================
#include "esc_telem.h"

#include <HardwareSerial.h>

#include "config.h"

namespace esc_telem {

// We use UART2 because UART0 is the USB console and UART1 has pin conflicts
// with the onboard flash on ESP32 DevKit v1.
static HardwareSerial& TELEM = Serial2;

static constexpr size_t PKT_LEN = 22;
static uint8_t  g_buf[PKT_LEN];
static size_t   g_idx = 0;
static Frame    g_latest{};

// ---------------------------------------------------------------------------
//  CRC16 XMODEM (poly 0x1021, init 0). APD uses this over the first 20 bytes.
// ---------------------------------------------------------------------------
static uint16_t crc16_xmodem(const uint8_t* data, size_t len) {
    uint16_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++) {
            if (crc & 0x8000) crc = (crc << 1) ^ 0x1021;
            else              crc <<= 1;
        }
    }
    return crc;
}

static inline uint16_t le16(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

// ---------------------------------------------------------------------------
void begin() {
    // Start UART2 at APD's default 115200 8N1. We only need RX - the TX pin
    // is passed as -1 so we don't drive any pin we don't want to drive.
    TELEM.begin(ESC_TELEM_BAUD, SERIAL_8N1, ESC_TELEM_RX_PIN, /*tx=*/-1);
    g_idx = 0;
    g_latest = {};
    Serial.printf("[esc_telem] UART%d RX=GPIO%d @ %d baud\n",
                  ESC_TELEM_UART, ESC_TELEM_RX_PIN, ESC_TELEM_BAUD);
}

// ---------------------------------------------------------------------------
//  Decode one candidate packet starting at g_buf[0]. On CRC success, fills
//  g_latest and returns true. On failure, returns false - caller will
//  advance one byte and retry (byte-level resync).
// ---------------------------------------------------------------------------
static bool try_decode() {
    uint16_t want = crc16_xmodem(g_buf, PKT_LEN - 2);
    uint16_t got  = le16(g_buf + 20);
    if (want != got) return false;

    Frame f{};
    f.rx_throttle     = le16(g_buf + 0);
    f.out_throttle    = le16(g_buf + 2);
    f.erpm            = (uint32_t)le16(g_buf + 4) * 100u;
    f.voltage_v       = le16(g_buf + 6)  * 0.01f;
    f.current_a       = le16(g_buf + 8)  * 0.01f;
    f.phase_current_a = le16(g_buf + 10) * 0.01f;
    f.mos_temp_c      = le16(g_buf + 12) * 0.01f;
    f.cap_temp_c      = le16(g_buf + 14) * 0.01f;
    f.mcu_temp_c      = le16(g_buf + 16) * 0.01f;
    f.timestamp_ms    = millis();
    f.valid           = true;
    g_latest = f;
    return true;
}

// ---------------------------------------------------------------------------
void update() {
    while (TELEM.available()) {
        uint8_t b = (uint8_t)TELEM.read();

        if (g_idx < PKT_LEN) {
            g_buf[g_idx++] = b;
        }

        if (g_idx == PKT_LEN) {
            if (try_decode()) {
                g_idx = 0;                 // full packet consumed
            } else {
                // CRC mismatch - shift one byte left and keep looking.
                // This is how we resync if we came up mid-packet.
                memmove(g_buf, g_buf + 1, PKT_LEN - 1);
                g_idx = PKT_LEN - 1;
            }
        }
    }
}

// ---------------------------------------------------------------------------
Frame latest() { return g_latest; }

}  // namespace esc_telem
