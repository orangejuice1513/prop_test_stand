/* load cell -> HX711 -> ESP32V1 -> laptop*/

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include "HX711.h"

// --- Hardware Pins ---
const int LC1_DT = 16;
const int LC2_DT = 17;
const int SHARED_SCK = 4;

HX711 scale_left;
HX711 scale_right;

// --- Networking ---
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
const char* laptop_ip = "192.168.1.XX"; // Change to your Mac's IP
const int port = 4210;

WiFiUDP udp;

void setup() {
    Serial.begin(115200);

    // Initialize Scales
    scale_left.begin(LC1_DT, SHARED_SCK);
    scale_right.begin(LC2_DT, SHARED_SCK);
    
    // Calibration (You'll need to find these factors later)
    scale_left.set_scale(1.0); 
    scale_right.set_scale(1.0);
    scale_left.tare();
    scale_right.tare();

    // Connect WiFi
    Serial.printf("Connecting to %s", ssid);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi Connected");
    Serial.print("ESP32 IP:   "); Serial.println(WiFi.localIP());
    Serial.printf("Sending to: %s:%d\n", laptop_ip, port);
}

void loop() {
    if (scale_left.is_ready() && scale_right.is_ready()) {
        long t = millis();
        float left = scale_left.get_units(1);
        float right = scale_right.get_units(1);

        // Package data as CSV string
        char msg[64];
        snprintf(msg, sizeof(msg), "%lu,%.3f,%.3f", t, left, right);

        // Send over UDP
        udp.beginPacket(laptop_ip, port);
        udp.print(msg);
        udp.endPacket();

        // Also print to Serial for debugging
        Serial.println(msg);
    }
}