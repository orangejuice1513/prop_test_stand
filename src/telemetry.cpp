// =============================================================================
//  telemetry.cpp  -  WiFi UDP streaming + command parser
// =============================================================================
#include "telemetry.h"

#include <WiFi.h>
#include <WiFiUdp.h>

#include "config.h"

namespace telemetry {

static WiFiUDP    g_send_udp;
static WiFiUDP    g_recv_udp;
static IPAddress  g_host_ip;
static uint32_t   g_last_send_ms    = 0;
static uint32_t   g_last_cmd_ms     = 0;
static const uint32_t kPeriodMs     = 1000 / TELEM_RATE_HZ;
static char       g_rx_buf[64];

// ---------------------------------------------------------------------------
void begin() {
    g_host_ip.fromString(UDP_HOST_IP);

    Serial.printf("[telemetry] connecting to \"%s\"...\n", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
        delay(250);
        Serial.print('.');
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.print("[telemetry] IP   = "); Serial.println(WiFi.localIP());
        Serial.print("[telemetry] host = "); Serial.println(g_host_ip);
        Serial.printf("[telemetry] listen port %u, send port %u @ %u Hz\n",
                      UDP_LISTEN_PORT, UDP_SEND_PORT, TELEM_RATE_HZ);
    } else {
        Serial.println("[telemetry] WiFi FAILED - running offline");
    }

    g_recv_udp.begin(UDP_LISTEN_PORT);
    g_last_cmd_ms = millis();
}

// ---------------------------------------------------------------------------
bool connected() { return WiFi.status() == WL_CONNECTED; }

// ---------------------------------------------------------------------------
void maybe_send(const Sample& s) {
    uint32_t now = millis();
    if (now - g_last_send_ms < kPeriodMs) return;
    g_last_send_ms = now;

    if (!connected()) return;

    // Fixed-width CSV keeps receiver.py parsing simple.
    char line[160];
    int n = snprintf(line, sizeof(line),
                     "%.2f,%.2f,%.2f,%.2f,%.1f,%lu,%.2f,%.2f,%.1f,%lu\n",
                     s.cell1_g, s.cell2_g, s.total_g, s.diff_g,
                     s.throttle_pct, (unsigned long)s.rpm,
                     s.voltage_v, s.current_a, s.temp_c,
                     (unsigned long)now);
    if (n <= 0) return;

    g_send_udp.beginPacket(g_host_ip, UDP_SEND_PORT);
    g_send_udp.write((const uint8_t*)line, n);
    g_send_udp.endPacket();
}

// ---------------------------------------------------------------------------
//  Parse one UDP datagram into an Event. Accepts upper or lower case.
// ---------------------------------------------------------------------------
static Event parse(const char* s) {
    Event e{Command::NONE, 0.0f};
    if (!s || !*s) return e;

    // Upper-case the first few letters for simple matching.
    char tag[8] = {0};
    size_t i = 0;
    while (i < sizeof(tag) - 1 && s[i] && s[i] != ' ' && s[i] != '\n' && s[i] != '\r') {
        tag[i] = (char)toupper((unsigned char)s[i]);
        i++;
    }

    if      (!strcmp(tag, "ARM"))  e.cmd = Command::ARM;
    else if (!strcmp(tag, "KILL")) e.cmd = Command::KILL;
    else if (!strcmp(tag, "TARE")) e.cmd = Command::TARE;
    else if (!strcmp(tag, "CAL"))  e.cmd = Command::CALIBRATE;
    else if (!strcmp(tag, "RAMP")) e.cmd = Command::RAMP;
    else if (tag[0] == 'T' && isdigit((unsigned char)tag[1])) {
        // T0..T100 style command.
        e.cmd   = Command::THROTTLE;
        e.value = (float)atoi(tag + 1);
    }
    return e;
}

// ---------------------------------------------------------------------------
Event poll_command() {
    int sz = g_recv_udp.parsePacket();
    if (sz <= 0) return {Command::NONE, 0.0f};

    int n = g_recv_udp.read(g_rx_buf, sizeof(g_rx_buf) - 1);
    if (n <= 0) return {Command::NONE, 0.0f};
    g_rx_buf[n] = 0;

    g_last_cmd_ms = millis();
    Event e = parse(g_rx_buf);
    Serial.printf("[telemetry] RX cmd: \"%s\"\n", g_rx_buf);
    return e;
}

// ---------------------------------------------------------------------------
uint32_t ms_since_last_command() { return millis() - g_last_cmd_ms; }

}  // namespace telemetry
