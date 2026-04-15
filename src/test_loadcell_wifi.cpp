// =============================================================================
//  test_loadcell_wifi.cpp  -  Wireless HX711 bring-up sketch (AP mode)
// =============================================================================
//  Direct port of Julia's known-working Arduino sketch into the PlatformIO
//  project tree. Keeps the exact SSID, password, port and wire format so the
//  behaviour is identical to what she already validated on hardware.
//
//  Mode: the ESP32 creates its OWN WiFi access point (AP mode). The laptop
//  joins that network and lands on 192.168.4.2 via DHCP. The ESP32 itself
//  is at 192.168.4.1.
//
//  SSID:     ThrustStand
//  Password: 12345678
//  Port:     4210  (used for both send AND receive)
//
//  Wire format (sent ~50 Hz as ASCII UDP datagrams):
//      T1: <raw1> | T2: <raw2>\n
//
//  The "raw" numbers are HX711 ADC counts from get_value(1): one sample,
//  tare offset applied, no gram conversion. This matches the Arduino sketch
//  and is what the Python listener expects. Unit conversion comes later,
//  once calibration is run.
//
//  Build / flash:
//      pio run -e test_loadcell_wifi -t upload
//      pio device monitor
// =============================================================================

#include <Arduino.h>
#include <HX711.h>
#include <WiFi.h>
#include <WiFiUdp.h>

#include "config.h"

// -- Access point credentials (match the Arduino sketch) --------------------
static const char*   AP_SSID     = "ThrustStand";
static const char*   AP_PASSWORD = "12345678";
static constexpr uint16_t UDP_PORT = 4210;

// In AP mode the ESP32 is always 192.168.4.1 and the first client (our Mac)
// lands on 192.168.4.2. Matches the Arduino sketch exactly.
static const IPAddress MAC_IP(192, 168, 4, 2);

static HX711   scale1;
static HX711   scale2;
static WiFiUDP udp;
static char    g_rx_buf[256];

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println();
    Serial.println("--- Booting Thrust Stand (AP Mode) ---");

    // 1. Start ESP32 as Access Point ----------------------------------------
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    delay(100);
    Serial.print("AP IP address: ");
    Serial.println(WiFi.softAPIP());

    // 2. Start UDP listener -------------------------------------------------
    udp.begin(UDP_PORT);
    Serial.printf("UDP listening on port %u\n", UDP_PORT);

    // 3. Initialize load cells ----------------------------------------------
    Serial.println("Initializing HX711s...");
    Serial.printf("  cell1: DT=GPIO%d  SCK=GPIO%d\n", LC1_DOUT, LC1_SCK);
    Serial.printf("  cell2: DT=GPIO%d  SCK=GPIO%d\n", LC2_DOUT, LC2_SCK);
    scale1.begin(LC1_DOUT, LC1_SCK);
    scale2.begin(LC2_DOUT, LC2_SCK);

    Serial.println("Taring scales. Do not touch the stand!");
    scale1.tare();
    scale2.tare();
    Serial.println("Scales zeroed. Commencing telemetry.");
}

void loop() {
    // --- RECEIVE from Mac (for future commands) ----------------------------
    int packet_size = udp.parsePacket();
    if (packet_size > 0) {
        int len = udp.read(g_rx_buf, sizeof(g_rx_buf) - 1);
        if (len > 0) g_rx_buf[len] = '\0';
        Serial.print("RECEIVED from Mac: ");
        Serial.println(g_rx_buf);
    }

    // --- SEND sensor data to Mac -------------------------------------------
    // Only read/send when BOTH scales are ready to avoid blocking the loop.
    if (scale1.is_ready() && scale2.is_ready()) {
        long reading1 = scale1.get_value(1);
        long reading2 = scale2.get_value(1);

        udp.beginPacket(MAC_IP, UDP_PORT);
        udp.printf("T1: %ld | T2: %ld\n", reading1, reading2);
        udp.endPacket();

        // Also mirror to USB serial so you can see it while you connect the
        // laptop to the ThrustStand network.
        Serial.printf("T1: %ld | T2: %ld\n", reading1, reading2);

        delay(20);  // stabilize the UDP stream, matches the Arduino sketch
    }
}
