#pragma once

// ---- WiFi credentials ----
#define WIFI_SSID     "YOUR_SSID"
#define WIFI_PASSWORD "YOUR_PASSWORD"

// ---- TCP server settings ----
#define TCP_PORT         5005   // port Python will connect to
#define SEND_INTERVAL_MS 10    // ms between samples (100 Hz); HX711 maxes at ~80 Hz at 80 SPS mode

// ---- HX711 pin definitions ----
// Load cell 1
#define LC1_DOUT  16
#define LC1_SCK   17

// Load cell 2
#define LC2_DOUT  18
#define LC2_SCK   19

// ---- HX711 calibration ----
// Run with CALIBRATE true first to find your scale factors:
//   1. Put a known weight on each load cell
//   2. Read the raw value from the serial monitor
//   3. scale_factor = raw_value / known_weight_in_your_units
#define CALIBRATE false

#define LC1_SCALE_FACTOR  1.0f  // raw / unit (e.g. N or lbf)
#define LC1_OFFSET        0L    // tare offset (set after calling tare())

#define LC2_SCALE_FACTOR  1.0f
#define LC2_OFFSET        0L
