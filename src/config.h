#ifndef CONFIG_H
#define CONFIG_H

// --- Pin Definitions ---
// RS-232 to Rice Lake 920i (via MAX3232 level shifter)
// Pins set via platformio.ini build_flags per board:
//   Super Mini:  RX=20, TX=21, LED=8
//   DevKit C:    RX=16, TX=17, LED=2
#ifndef RS232_RX_PIN
#define RS232_RX_PIN 20
#endif
#ifndef RS232_TX_PIN
#define RS232_TX_PIN 21
#endif
#ifndef STATUS_LED_PIN
#define STATUS_LED_PIN 8
#endif

// --- Serial Configuration (920i) ---
// Port 1: 9600 8N1 CR/LF
// Port 2: 115200 8N2 CR/LF
#define RS232_BAUD 9600
#define RS232_CONFIG SERIAL_8N1  // 8 data bits, no parity, 1 stop bit

// The 920i terminates lines with CR/LF
#define RS232_LINE_TERMINATOR "\r\n"

// --- Data Capture ---
// Timeout (ms) after last received byte to consider a dump complete.
// At 9600 baud, one character takes ~1ms. A 40KB dump takes ~40 seconds.
// 2 seconds of silence means the dump is finished.
#define DUMP_COMPLETE_TIMEOUT_MS 2000

// Maximum dump size in bytes (920i dumps are ~40KB max)
#define DUMP_BUFFER_SIZE 50000

// --- Device Identity ---
#define DEVICE_HOSTNAME "brakemachine"
#define DEVICE_NAME "brakeMachine"

// --- Network ---
#define AP_SSID "brakeMachine-setup"
#define AP_PASSWORD "configure"  // Minimum 8 chars for WPA2

// NTP server
#define NTP_SERVER "pool.ntp.org"
// UTC offset in seconds (US Mountain = -7h = -25200)
#define DEFAULT_UTC_OFFSET -25200
#define DEFAULT_DST_OFFSET 3600

// --- HTTP Upload ---
#define UPLOAD_ENDPOINT "/upload"
#define UPLOAD_TIMEOUT_MS 10000
#define MAX_QUEUED_DUMPS 10
#define DEFAULT_RECEIVER_URL ""  // Empty — must configure via web dashboard
#define QUEUE_DIR "/queue"
#define UPLOAD_RETRY_INTERVAL_MS 30000
#define DUMP_PREVIEW_LINES 3

// --- Web Server ---
#define WEB_SERVER_PORT 80

#endif // CONFIG_H
