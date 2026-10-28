// Compile-time configuration: file paths, defaults and limits for every setting, the LED pin and the portal address.
//
// This is the one place to change behaviour without touching the code. Values that the user can also change on the
// admin pages (marked "defaults") only apply until the user saves their own; the saved values live in NVS and win.
// Values marked "limits" bound what both the pages offer and validateSettings() accepts.
#pragma once
#include <Arduino.h>

// Firmware version, printed in the serial log at start-up. Bump it with every release.
#define APP_VERSION "0.5.407"

// ---------------------------------------------------------------------------------------------------------------
// Storage
// ---------------------------------------------------------------------------------------------------------------

// Everything under data/ is uploaded to LittleFS as-is, so data/index.html.gz becomes /index.html.gz. The gzipped
// pages and stylesheet are generated from web/ by scripts/pack_files.py; each is served at its own route rather than
// by its file name.

#define HOME_PAGE_PATH "/index.html.gz"
#define ADMIN_PAGE_PATH "/admin.html.gz"
#define NETWORK_PAGE_PATH "/network.html.gz"
#define CSS_FILE_PATH "/bootstrap.css.gz"

// Images and other static files (data/files), served at the site root, so data/files/logo.png becomes /logo.png.
// The favicon is the exception: it lives at data/favicon.ico. Directory paths end in '/'.
#define STATIC_FILES_DIR "/files/"

// NVS (non-volatile storage) namespace holding the device settings and admin credentials. NVS is a separate flash
// partition, so these values survive both firmware uploads and filesystem uploads.
// NVS limits namespace names to 15 characters; a longer name makes every read and write fail.
#define PREFS_NAMESPACE "netbase-app"

// ---------------------------------------------------------------------------------------------------------------
// Administration
// ---------------------------------------------------------------------------------------------------------------

// HTTP basic authentication realm for the protected admin area, shown by the browser's login prompt.
#define ADMIN_REALM "/admin/"

// Admin login used until the password is changed on /admin/. Change it on first use.
#define DEFAULT_ADMIN_USER "admin"
#define DEFAULT_ADMIN_PASSWORD "password"

// Delay between saving settings and restarting, so the confirmation reaches the browser first.
#define RESTART_DELAY_MS 3500

// ---------------------------------------------------------------------------------------------------------------
// Request activity LED
// ---------------------------------------------------------------------------------------------------------------

// GPIO of the LED that blinks on every HTTP request: GPIO 2 is the on-board LED of the Freenove WROVER board.
// Do not use GPIO 1 or 3; they are the UART0 TX/RX pins used by the serial console.
#define REQUEST_LED_PIN 2

// How long the LED stays lit after a request, in milliseconds.
#define REQUEST_LED_BLINK_MS 50

// ---------------------------------------------------------------------------------------------------------------
// Access point (defaults and limits for the settings on /admin/network/)
// ---------------------------------------------------------------------------------------------------------------

// Maximum number of stations that may join the access point at once. The ESP32 allows up to 10, but every
// connected phone keeps several TCP sockets open to the portal, so a small number keeps memory use predictable.
#define DEFAULT_MAX_CLIENTS 4
#define MAX_CLIENTS_MIN 1
#define MAX_CLIENTS_MAX 10

// 2.4 GHz channel used by the access point. Channels 1, 6 and 11 are the only non-overlapping ones. Channels 12 and
// 13 are allowed in Europe and most of the world but not in North America.
#define DEFAULT_WIFI_CHANNEL 6
#define WIFI_CHANNEL_MIN 1
#define WIFI_CHANNEL_MAX 13

// How long loop() sleeps between polls of the captive DNS server, in milliseconds. Every DNS query a phone sends
// waits for the next poll, and a single page load triggers many queries, so a shorter interval means faster pages
// at the cost of a little more CPU time. The network page offers every value in the range.
#define DEFAULT_DNS_INTERVAL 10
#define DNS_INTERVAL_MIN 5
#define DNS_INTERVAL_MAX 20

// ---------------------------------------------------------------------------------------------------------------
// Radio regulation (defaults for the settings on /admin/network/, applied in both Wi-Fi modes)
// ---------------------------------------------------------------------------------------------------------------

// Regulatory region: "01" is ESP-IDF's world-safe mode, the driver's own default, which keeps to rules that are
// legal everywhere. Pick the actual country on the network page to unlock its full channel range and power limits.
#define DEFAULT_WIFI_COUNTRY "01"

// Transmit power cap in quarter-dBm: 78 = 19.5 dBm, the highest level the Arduino core offers. Lower it to shrink
// the range of the access point (for example to keep the portal to one room) and to reduce power draw and heat.
#define DEFAULT_TX_POWER 78

// The access point's address. It is deliberately a public-range address: some Android builds (notably Samsung)
// refuse to show the captive portal sign-in when the portal lives on a private network such as 192.168.x.x.
// `inline` gives one shared object for the whole program instead of a copy in every file that includes this header.
inline const IPAddress localIP(4, 3, 2, 1);
inline const IPAddress subnetMask(255, 255, 255, 0);

// The same address as text, used to recognise requests addressed to the portal (Host header) and as the target of
// every captive-portal redirect. Kept as compile-time strings so no String objects are built at start-up.
inline constexpr char localIPHost[] = "4.3.2.1";
inline constexpr char localIPURL[] = "http://4.3.2.1/";

// Password for the access point: nullptr makes it an open network, which is what a public portal wants.
// A WPA2 password must be 8 to 63 characters, e.g. inline constexpr const char *apPassword = "12345678";
inline constexpr const char *apPassword = nullptr;

// ---------------------------------------------------------------------------------------------------------------
// Station (joining an existing Wi-Fi network)
// ---------------------------------------------------------------------------------------------------------------

// How long to keep trying to join the configured network at start-up before giving up and starting the access
// point instead. Falling back guarantees the setup page is always reachable, even with a wrong password.
#define STATION_CONNECT_TIMEOUT_MS 20000