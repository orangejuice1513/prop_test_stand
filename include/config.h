#pragma once
// =============================================================================
//  Propeller Test Stand - Central Configuration
// =============================================================================
//  All pin assignments, WiFi credentials, network ports and default
//  calibration values live here so the rest of the codebase does not
//  contain magic numbers. Edit this file to match your wiring / network.
// =============================================================================

// ---------------------------- WiFi credentials -------------------------------
#define WIFI_SSID         "YOUR_SSID"
#define WIFI_PASSWORD     "YOUR_PASSWORD"

// ---------------------------- Network settings -------------------------------
// UDP streaming: ESP32 sends telemetry TO this host/port, and LISTENS for
// command packets on UDP_LISTEN_PORT.
#define UDP_HOST_IP       "192.168.1.100"   // laptop running receiver.py
#define UDP_SEND_PORT     9000              // telemetry destination port
#define UDP_LISTEN_PORT   9001              // ESP32 listens here for commands
#define TELEM_RATE_HZ     20                // CSV push rate (Hz)

// ---------------------------- HX711 load cells -------------------------------
// Load cell 1  -> HX711 #1
#define LC1_DOUT          4      // HX711 #1 DT  -> ESP32 GPIO4
#define LC1_SCK           16     // HX711 #1 SCK -> ESP32 GPIO16
// Load cell 2  -> HX711 #2
#define LC2_DOUT          17     // HX711 #2 DT  -> ESP32 GPIO17
#define LC2_SCK           18     // HX711 #2 SCK -> ESP32 GPIO18

// Default calibration factors (grams). These get overwritten at runtime after
// you run the CAL sequence (from receiver.py or Serial). Keep as a sane
// starting point so the raw numbers at least look plausible.
#define LC1_DEFAULT_FACTOR  (-420.0f)
#define LC2_DEFAULT_FACTOR  (-420.0f)

// ---------------------------- ESC signal / telemetry ------------------------
#define ESC_SIGNAL_PIN    19     // PWM / DShot output -> APD ESC signal wire
#define ESC_TELEM_RX_PIN  13     // UART2 RX <- APD "T" telemetry wire (3.3V)
#define ESC_TELEM_BAUD    115200 // APD F-series default
#define ESC_TELEM_UART    2      // we use UART2 (Serial2)

// ---------------------------- PWM ESC settings ------------------------------
// Standard servo PWM: 50 Hz, 1000 us (off) .. 2000 us (full).
#define PWM_FREQ_HZ       50
#define PWM_RESOLUTION    16        // bits - 65536 steps at 50 Hz
#define PWM_CHANNEL       0         // ledc channel
#define PWM_MIN_US        1000
#define PWM_MAX_US        2000
#define PWM_ARM_MS        3000      // hold min throttle this long on boot

// ---------------------------- DShot settings --------------------------------
// DShot600 throttle range is 48..2047. 0..47 are reserved commands.
// This project runs DShot in UNIDIRECTIONAL mode only - RPM comes from the
// APD "T" UART telemetry wire, not from the DShot signal line, so no
// pull-up resistor is needed on GPIO19.
#define DSHOT_MIN         48
#define DSHOT_MAX         2047
// The FreeRTOS sender task pushes a frame every DSHOT_PERIOD_MS ms.
#define DSHOT_PERIOD_MS   2

// ---------------------------- Safety ----------------------------------------
// Max throttle percent allowed via the T<n> command. Hard ceiling - protects
// you from fat fingers. Raise at your own risk.
#define SAFETY_MAX_THROTTLE_PCT 100
// Auto-kill if no command is received for this many ms while armed. Think of
// it as a deadman switch: if the laptop/radio dies, the motor spools down.
#define COMMAND_TIMEOUT_MS      2000
