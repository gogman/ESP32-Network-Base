// Device settings that the user edits on /admin/ and /admin/network/, stored in NVS.
//
// NVS (non-volatile storage) is its own flash partition, separate from both the firmware and the LittleFS
// filesystem, so these settings survive firmware and filesystem uploads. Network settings are read once at
// start-up; changing them on the network page saves them and restarts the device.
#pragma once
#include <Arduino.h>

// How the device connects to Wi-Fi.
enum class WifiMode
{
    AccessPoint, // run its own network with the captive portal (the default)
    Station,     // join an existing Wi-Fi network as a client
};

struct DeviceSettings
{
    WifiMode mode = WifiMode::AccessPoint;
    String apSsid;      // name of the device's own network in AccessPoint mode
    String hostname;    // name announced to DHCP and mDNS (http://<hostname>.local/)
    String staSsid;     // network to join in Station mode
    String staPassword; // that network's password; empty for an open network

    // Access point tuning, used only in AccessPoint mode. Defaults and ranges are in App_Config.h.
    uint8_t maxClients = 0;  // stations allowed to join at once
    uint8_t wifiChannel = 0; // 2.4 GHz channel
    uint8_t dnsInterval = 0; // ms between captive DNS server polls in loop()

    // Radio regulation, applied in both modes.
    String country;     // code from kWifiCountries; sets the legal channels and power limits for that region
    int8_t txPower = 0; // transmit power cap in quarter-dBm, one of the levels in kTxPowerLevels

    // Login for the admin area. Stored as plain text, because the basic authentication middleware needs it.
    String adminUser;
    String adminPassword;
};

// A regulatory region the Wi-Fi driver knows. ESP-IDF applies that region's channel and power rules when its code
// is passed to esp_wifi_set_country_code(); the list is the one ESP-IDF supports. "01" is ESP-IDF's world-safe
// default, which restricts itself to rules valid everywhere.
struct WifiCountry
{
    const char *code;
    const char *name;
    uint8_t maxChannel; // highest 2.4 GHz channel an access point may use there
};
extern const WifiCountry kWifiCountries[];
extern const size_t kWifiCountryCount;

// Returns the entry for `code`, or nullptr if it isn't in the list.
const WifiCountry *findCountry(const String &code);

// A transmit power level offered on the network page. The value is in quarter-dBm units, as the Wi-Fi driver takes
// it (78 = 19.5 dBm); these are the levels the Arduino core defines in wifi_power_t.
struct TxPowerLevel
{
    int8_t quarterDbm;
    const char *label;
};
extern const TxPowerLevel kTxPowerLevels[];
extern const size_t kTxPowerLevelCount;

// Reads the settings from NVS. Anything never set gets its default. The names and the admin login are saved straight
// away so they stay the same across restarts; the numeric settings simply fall back to their defaults until first saved:
//   apSsid       "ESP32 Network Base XXXXXX", XXXXXX being six random hexadecimal digits picked on the first start-up,
//                so several devices in one place get distinct network names
//   hostname     "esp32-network-base"
//   adminUser    DEFAULT_ADMIN_USER
//   adminPassword DEFAULT_ADMIN_PASSWORD
//   maxClients   DEFAULT_MAX_CLIENTS
//   wifiChannel  DEFAULT_WIFI_CHANNEL
//   dnsInterval  DEFAULT_DNS_INTERVAL
//   country      DEFAULT_WIFI_COUNTRY
//   txPower      DEFAULT_TX_POWER
// A stored value outside its allowed range or list (e.g. after the range is narrowed in a firmware update) is replaced
// by the default too.
DeviceSettings loadSettings();

// Writes all settings to NVS and verifies them by reading them back. Returns false if any value didn't stick.
bool saveSettings(const DeviceSettings &settings);

// Checks the settings against Wi-Fi and DNS limits. Returns a message describing the first problem, suitable for
// showing to the user, or an empty string if everything is valid.
String validateSettings(const DeviceSettings &settings);
