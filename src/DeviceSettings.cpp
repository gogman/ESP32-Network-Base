#include "DeviceSettings.h"
#include "App_Config.h"
#include "SerialLog.h"

#include <Preferences.h>

// Settings are stored with the Arduino Preferences library, a thin layer over ESP-IDF's NVS. NVS is a key-value store
// in its own flash partition: each value is written under a short key inside a namespace (PREFS_NAMESPACE). Writes
// are wear-levelled and survive power loss part-way through.
//
// Each setting is its own key, so adding a setting later doesn't disturb the existing ones: an old device simply
// has no value for the new key yet and loadSettings() fills in the default. Never rename a key, or the stored
// value is lost; never reuse one for a different type.

// Catch a namespace that is too long at compile time; at run time it would only show up as every NVS call failing.
static_assert(sizeof(PREFS_NAMESPACE) - 1 <= 15, "NVS limits namespace names to 15 characters");

// NVS key names. NVS limits keys to 15 characters.
static constexpr const char *kMode = "wifi_mode";
static constexpr const char *kApSsid = "ap_ssid";
static constexpr const char *kHostname = "hostname";
static constexpr const char *kStaSsid = "sta_ssid";
static constexpr const char *kStaPassword = "sta_pass";
static constexpr const char *kMaxClients = "max_clients";
static constexpr const char *kWifiChannel = "wifi_channel";
static constexpr const char *kDnsInterval = "dns_interval";
static constexpr const char *kCountry = "country";
static constexpr const char *kTxPower = "tx_power";
static constexpr const char *kAdminUser = "admin_user";
static constexpr const char *kAdminPassword = "admin_password";

// Every country code ESP-IDF's esp_wifi_set_country_code() accepts (see esp_wifi.h). The channel limit is what an
// access point may use: North America, Mexico and Taiwan stop at 11; most other regions allow 13 (Japan's
// channel 14 is 802.11b-only and not used). World-safe mode is held to 11 because channels 12 and 13 are only
// scanned passively there, and an access point has to transmit.
const WifiCountry kWifiCountries[] = {
    {"01", "World (safe mode)", 11},
    {"AT", "Austria", 13},
    {"AU", "Australia", 13},
    {"BE", "Belgium", 13},
    {"BG", "Bulgaria", 13},
    {"BR", "Brazil", 13},
    {"CA", "Canada", 11},
    {"CH", "Switzerland", 13},
    {"CN", "China", 13},
    {"CY", "Cyprus", 13},
    {"CZ", "Czechia", 13},
    {"DE", "Germany", 13},
    {"DK", "Denmark", 13},
    {"EE", "Estonia", 13},
    {"ES", "Spain", 13},
    {"FI", "Finland", 13},
    {"FR", "France", 13},
    {"GB", "United Kingdom", 13},
    {"GR", "Greece", 13},
    {"HK", "Hong Kong", 13},
    {"HR", "Croatia", 13},
    {"HU", "Hungary", 13},
    {"IE", "Ireland", 13},
    {"IN", "India", 13},
    {"IS", "Iceland", 13},
    {"IT", "Italy", 13},
    {"JP", "Japan", 13},
    {"KR", "South Korea", 13},
    {"LI", "Liechtenstein", 13},
    {"LT", "Lithuania", 13},
    {"LU", "Luxembourg", 13},
    {"LV", "Latvia", 13},
    {"MT", "Malta", 13},
    {"MX", "Mexico", 11},
    {"NL", "Netherlands", 13},
    {"NO", "Norway", 13},
    {"NZ", "New Zealand", 13},
    {"PL", "Poland", 13},
    {"PT", "Portugal", 13},
    {"RO", "Romania", 13},
    {"SE", "Sweden", 13},
    {"SI", "Slovenia", 13},
    {"SK", "Slovakia", 13},
    {"TW", "Taiwan", 11},
    {"US", "United States", 11},
};
// Computed here, where the array's size is known; other files only see the extern declaration without a size.
const size_t kWifiCountryCount = sizeof(kWifiCountries) / sizeof(kWifiCountries[0]);

// The Arduino core's wifi_power_t levels, strongest first. The page lists them in this order, so the maximum is
// the first choice. The driver accepts other values too, but limiting the choice to these keeps the value one that
// WiFi.setTxPower() is known to handle.
const TxPowerLevel kTxPowerLevels[] = {
    {78, "19.5 dBm (maximum)"},
    {76, "19 dBm"},
    {74, "18.5 dBm"},
    {68, "17 dBm"},
    {60, "15 dBm"},
    {52, "13 dBm"},
    {44, "11 dBm"},
    {34, "8.5 dBm"},
    {28, "7 dBm"},
    {20, "5 dBm"},
    {8, "2 dBm"},
    {-4, "-1 dBm (minimum)"},
};
const size_t kTxPowerLevelCount = sizeof(kTxPowerLevels) / sizeof(kTxPowerLevels[0]);

const WifiCountry *findCountry(const String &code)
{
    for (const WifiCountry &c : kWifiCountries)
        if (code == c.code)
            return &c;
    return nullptr;
}

// True if `quarterDbm` is one of the levels in kTxPowerLevels.
static bool isTxPowerLevel(int8_t quarterDbm)
{
    for (const TxPowerLevel &level : kTxPowerLevels)
        if (level.quarterDbm == quarterDbm)
            return true;
    return false;
}

// Wi-Fi limits: an SSID is at most 32 bytes; a WPA2 passphrase is 8 to 63 characters.
static constexpr size_t kMaxSsidLength = 32;
static constexpr size_t kMinPasswordLength = 8;
static constexpr size_t kMaxPasswordLength = 63;

// Host names are kept to 32 characters, well inside the 63-character DNS label limit, because the ESP-IDF network
// stack stores the host name in a 32-byte buffer.
static constexpr size_t kMaxHostnameLength = 32;

// The admin user name has no protocol limit; 32 characters keeps it in line with the other names.
static constexpr size_t kMaxUserNameLength = 32;

// Preferences::getString() logs an error for a key that doesn't exist, which is the normal state before the first
// save; checking isKey() first keeps the start-up log free of false alarms.
static String readString(Preferences &prefs, const char *key)
{
    return prefs.isKey(key) ? prefs.getString(key) : String();
}

// True if min <= value <= max.
static bool inRange(uint8_t value, uint8_t min, uint8_t max)
{
    return value >= min && value <= max;
}

// Reads a small number stored as one byte, returning `fallback` if it was never saved or lies outside min..max.
static uint8_t readByte(Preferences &prefs, const char *key, uint8_t fallback, uint8_t min, uint8_t max)
{
    if (!prefs.isKey(key))
        return fallback;
    uint8_t value = prefs.getUChar(key, fallback);
    return inRange(value, min, max) ? value : fallback;
}

DeviceSettings loadSettings()
{
    Preferences prefs;

    // Read-write, because missing defaults are saved below. If NVS can't be opened, every setting gets its default.
    if (!prefs.begin(PREFS_NAMESPACE, false))
        DEBUG_SERIAL(Serial.println("Settings: NVS namespace couldn't be opened, using defaults"));

    // Step 1: read every stored value. Text settings come back empty if never saved; numeric ones get their default
    // straight away, and so do values outside the current limits.
    DeviceSettings settings;
    // Anything but "sta", including nothing stored yet, means access point mode, so a new device starts the portal.
    settings.mode = readString(prefs, kMode) == "sta" ? WifiMode::Station : WifiMode::AccessPoint;
    settings.apSsid = readString(prefs, kApSsid);
    settings.hostname = readString(prefs, kHostname);
    settings.staSsid = readString(prefs, kStaSsid);
    settings.staPassword = readString(prefs, kStaPassword);
    settings.maxClients = readByte(prefs, kMaxClients, DEFAULT_MAX_CLIENTS, MAX_CLIENTS_MIN, MAX_CLIENTS_MAX);
    settings.wifiChannel = readByte(prefs, kWifiChannel, DEFAULT_WIFI_CHANNEL, WIFI_CHANNEL_MIN, WIFI_CHANNEL_MAX);
    settings.dnsInterval = readByte(prefs, kDnsInterval, DEFAULT_DNS_INTERVAL, DNS_INTERVAL_MIN, DNS_INTERVAL_MAX);

    settings.country = readString(prefs, kCountry);
    if (!findCountry(settings.country))
        settings.country = DEFAULT_WIFI_COUNTRY;

    // Stored as a signed byte because the lowest level (-1 dBm) is negative.
    settings.txPower = prefs.isKey(kTxPower) ? prefs.getChar(kTxPower, DEFAULT_TX_POWER) : DEFAULT_TX_POWER;
    if (!isTxPowerLevel(settings.txPower))
        settings.txPower = DEFAULT_TX_POWER;

    // Step 2: fill in the names and the admin login if they are missing, and save them at once. Unlike the numeric
    // defaults these must not change between start-ups: a random network name that changed on every restart would
    // make phones forget the network, and the user has to know the login to get in.
    if (settings.apSsid.isEmpty())
    {
        // esp_random() is the hardware random number generator. It is only fully random once the radio is running,
        // which it isn't yet, but the suffix just needs to differ between neighbouring devices, not be secret.
        char name[26]; // "ESP32 Network Base " (19) + 6 hex digits + terminating zero
        snprintf(name, sizeof(name), "ESP32 Network Base %06lX", (unsigned long)(esp_random() & 0xFFFFFF));
        settings.apSsid = name;
        prefs.putString(kApSsid, settings.apSsid);
    }
    if (settings.hostname.isEmpty())
    {
        settings.hostname = "esp32-network-base";
        prefs.putString(kHostname, settings.hostname);
    }

    settings.adminUser = readString(prefs, kAdminUser);
    settings.adminPassword = readString(prefs, kAdminPassword);
    if (settings.adminUser.isEmpty() || settings.adminPassword.isEmpty())
    {
        settings.adminUser = DEFAULT_ADMIN_USER;
        settings.adminPassword = DEFAULT_ADMIN_PASSWORD;
        prefs.putString(kAdminUser, settings.adminUser);
        prefs.putString(kAdminPassword, settings.adminPassword);
        DEBUG_SERIAL(Serial.printf("Admin login set to the default, user: %s  password: %s\n",
                                   settings.adminUser.c_str(), settings.adminPassword.c_str()));
    }

    // Close the namespace; NVS allows only a limited number of open handles.
    prefs.end();

    return settings;
}

bool saveSettings(const DeviceSettings &s)
{
    Preferences prefs;
    if (!prefs.begin(PREFS_NAMESPACE, false))
        return false;

    // putString() returns the number of bytes written, which is also 0 for a legitimately empty string, so success
    // is confirmed by reading every value back instead. Every value is written even after one fails, so a single bad
    // key doesn't leave the rest unsaved; `ok` just remembers that something went wrong.
    bool ok = true;
    auto put = [&](const char *key, const String &value)
    {
        prefs.putString(key, value);
        ok = ok && prefs.isKey(key) && prefs.getString(key) == value;
    };
    put(kMode, s.mode == WifiMode::Station ? "sta" : "ap");
    put(kApSsid, s.apSsid);
    put(kHostname, s.hostname);
    put(kStaSsid, s.staSsid);
    put(kStaPassword, s.staPassword);

    auto putByte = [&](const char *key, uint8_t value)
    {
        prefs.putUChar(key, value);
        ok = ok && prefs.isKey(key) && prefs.getUChar(key) == value;
    };
    putByte(kMaxClients, s.maxClients);
    putByte(kWifiChannel, s.wifiChannel);
    putByte(kDnsInterval, s.dnsInterval);
    put(kCountry, s.country);
    put(kAdminUser, s.adminUser);
    put(kAdminPassword, s.adminPassword);
    // Signed, unlike the other numbers, so it gets its own put/check rather than putByte.
    prefs.putChar(kTxPower, s.txPower);
    ok = ok && prefs.isKey(kTxPower) && prefs.getChar(kTxPower) == s.txPower;

    prefs.end();
    return ok;
}

// A host name is a single DNS label (RFC 1123): letters, digits and hyphens, not starting or ending with a hyphen.
static bool isValidHostname(const String &name)
{
    size_t len = name.length();
    if (len == 0 || len > kMaxHostnameLength || name[0] == '-' || name[len - 1] == '-')
        return false;
    for (size_t i = 0; i < len; i++)
        if (!isalnum((unsigned char)name[i]) && name[i] != '-')
            return false;
    return true;
}

// This is the authority on what may be saved. The pages check most of the same rules in the browser for a quicker
// answer, but a request can come from anywhere, so every rule is enforced here regardless. The checks run in the
// order of the fields on the pages, and only the first problem is reported.
String validateSettings(const DeviceSettings &s)
{
    // String::length() counts bytes, which is what the 32-byte SSID limit measures, so names containing multi-byte
    // UTF-8 characters are limited correctly.
    if (s.apSsid.isEmpty() || s.apSsid.length() > kMaxSsidLength)
        return "The access point name must be 1 to 32 characters.";
    if (!inRange(s.maxClients, MAX_CLIENTS_MIN, MAX_CLIENTS_MAX))
        return "Maximum clients must be between " + String(MAX_CLIENTS_MIN) + " and " + String(MAX_CLIENTS_MAX) + ".";
    if (!inRange(s.wifiChannel, WIFI_CHANNEL_MIN, WIFI_CHANNEL_MAX))
        return "The Wi-Fi channel must be between " + String(WIFI_CHANNEL_MIN) + " and " + String(WIFI_CHANNEL_MAX) + ".";
    if (!inRange(s.dnsInterval, DNS_INTERVAL_MIN, DNS_INTERVAL_MAX))
        return "The DNS interval must be between " + String(DNS_INTERVAL_MIN) + " and " + String(DNS_INTERVAL_MAX) + " ms.";

    // The channel was checked against the overall limit above; the country may allow fewer.
    const WifiCountry *country = findCountry(s.country);
    if (!country)
        return "Choose a country from the list.";
    if (s.wifiChannel > country->maxChannel)
        return "Channel " + String(s.wifiChannel) + " isn't allowed in " + country->name + "; choose 1 to " +
               String(country->maxChannel) + ".";
    if (!isTxPowerLevel(s.txPower))
        return "Choose a transmit power from the list.";
    if (!isValidHostname(s.hostname))
        return "The host name must be 1 to 32 letters, digits or hyphens, and can't start or end with a hyphen.";
    // The network to join is only required in station mode, but its limits apply whenever one is stored, so a
    // saved-but-unused name is still valid when the user switches modes later.
    if (s.mode == WifiMode::Station && s.staSsid.isEmpty())
        return "Enter the name (SSID) of the Wi-Fi network to join.";
    if (s.staSsid.length() > kMaxSsidLength)
        return "The Wi-Fi network name can be at most 32 characters.";
    if (!s.staPassword.isEmpty() && (s.staPassword.length() < kMinPasswordLength || s.staPassword.length() > kMaxPasswordLength))
        return "The Wi-Fi password must be 8 to 63 characters, or empty for an open network.";
    // Basic authentication sends "user:password", so a colon in the user name would split it in the wrong place.
    if (s.adminUser.isEmpty() || s.adminUser.length() > kMaxUserNameLength || s.adminUser.indexOf(':') >= 0)
        return "The admin user name must be 1 to 32 characters and can't contain a colon.";
    if (s.adminPassword.length() < kMinPasswordLength || s.adminPassword.length() > kMaxPasswordLength)
        return "The admin password must be 8 to 63 characters.";
    return String();
}
