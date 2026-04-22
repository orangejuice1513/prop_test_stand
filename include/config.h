
#pragma once
// =============================================================================
//  Propeller Test Stand - Central Configuration
// =============================================================================

// ---------------------------- WiFi (AP mode) --------------------------------
// The ESP32 creates its OWN WiFi network; the laptop joins it. No router,
// no shared SSID. These must match the strings in listen_loadcell.py and
// ground_station.py so the laptop side knows where to connect.
#define WIFI_SSID         "ThrustStand"
#define WIFI_PASSWORD     "12345678"        // >= 8 chars for WPA2

// ---------------------------- Network settings -------------------------------
// In AP mode the ESP32 is always 192.168.4.1 and the first DHCP client
// (our Mac) lands on 192.168.4.2. Telemetry is sent directly there.
// The same port is used for both send and receive.
#define UDP_HOST_IP       "192.168.4.2"     // laptop running ground_station.py
#define UDP_SEND_PORT     4210              // telemetry destination port
#define UDP_LISTEN_PORT   4210              // ESP32 listens here for commands
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
// APD 120F3 V2 wiring:
//
//   Signal header (3-pin, on the side of the PCB):
//     S  -> ESP32 GPIO 21   throttle (PWM or DShot)
//     -  -> ESP32 GND       signal ground
//     T  -> ESP32 GPIO 13   ESC -> ESP telemetry (115200 8N1, 3.3 V)
//
//   Auxiliary UART header (4-pin, on the bottom edge):
//     +  -> NC              3.3-5V logic-only power; leave open when battery
//                           is connected. The ESC's MCU is already powered
//                           from the main bus.
//     RX -> ESP32 GPIO 27   ESP -> ESC, used to push configuration commands
//                           to the APD bootloader / config protocol
//     TX -> ESP32 GPIO 13   ESC -> ESP, identical telemetry stream to the
//                           "T" pad. Wire ONE OR THE OTHER, not both.
//     G  -> ESP32 GND       UART ground
//
// Throttle still rides the dedicated S pin (APD does not accept throttle
// over the aux UART) - the aux UART is purely the bidirectional config /
// telemetry channel.
#define ESC_SIGNAL_PIN    33     // PWM / DShot output -> APD "S" pad
#define ESC_TELEM_RX_PIN  13     // UART2 RX <- APD "T" or aux "TX" pad
#define ESC_AUX_TX_PIN    27     // UART2 TX -> APD aux "RX" pad (config only)
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
// APD "T" / aux "TX" UART telemetry wire, not from the DShot signal line,
// so no pull-up resistor is needed on the signal pin.
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
