#include "NetworkServices.h"
#include "App_Config.h"
#include "SerialLog.h"

#include <DNSServer.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <esp_wifi.h>

// Wi-Fi is set up through two layers. The Arduino WiFi object covers the everyday calls (mode, softAP, begin,
// host name). The ESP-IDF esp_wifi_* functions underneath reach what Arduino doesn't expose: the country code and
// the driver's start-up configuration. Order matters with esp_wifi_*: most calls fail unless the driver has been
// initialised (WiFi.mode() does that) or started, which is why each helper below notes what it needs.

// The captive DNS server, created on the heap the first time the access point starts. It exists only in access
// point mode.
static DNSServer *dnsServer = nullptr;

// The mode that actually started, which differs from the saved mode after a fallback to the access point.
static WifiMode runningMode = WifiMode::AccessPoint;

// Applies the regulatory region. 802.11d is turned off so the chosen country always applies, instead of being
// replaced by whatever country a joined network advertises. Needs the driver initialised (after WiFi.mode()).
static void applyCountry(const DeviceSettings &settings)
{
    if (esp_wifi_set_country_code(settings.country.c_str(), false) != ESP_OK)
        DEBUG_SERIAL(Serial.printf("Couldn't set Wi-Fi country \"%s\"\n", settings.country.c_str()));
}

// Caps the transmit power. Needs the driver started, and must follow any driver restart, which resets it.
static void applyTxPower(const DeviceSettings &settings)
{
    // The stored value is already in the driver's quarter-dBm units and one of the wifi_power_t levels, so it is
    // passed through unchanged.
    if (!WiFi.setTxPower(static_cast<wifi_power_t>(settings.txPower)))
        DEBUG_SERIAL(Serial.printf("Couldn't set transmit power %d/4 dBm\n", settings.txPower));
}

// Starts the device's own network at localIP, and the DNS server that sends every name there.
static void startAccessPoint(const DeviceSettings &settings)
{
    // Initialises and starts the driver in access point mode. The country is set straight after, before the access
    // point begins transmitting, so it never broadcasts on a channel or at a power the region forbids.
    WiFi.mode(WIFI_MODE_AP);
    applyCountry(settings);

    // The setup page only saves a channel allowed in the chosen country, but settings saved by older firmware were
    // never checked against a country. An illegal channel would stop the access point from starting, leaving the
    // device unreachable, so fall back to the default channel instead.
    uint8_t channel = settings.wifiChannel;
    const WifiCountry *country = findCountry(settings.country);
    if (country && channel > country->maxChannel)
    {
        DEBUG_SERIAL(Serial.printf("Channel %u isn't allowed in %s, using %u\n", channel, country->name, DEFAULT_WIFI_CHANNEL));
        channel = DEFAULT_WIFI_CHANNEL;
    }

    // The portal is its own gateway: clients get 4.3.2.1 as both their gateway and their DNS server.
    WiFi.softAPConfig(localIP, localIP, subnetMask);

    // Arguments: network name, password (nullptr = open), channel, hidden (0 = the name is broadcast, so phones list
    // the network), and the most stations allowed to join at once.
    WiFi.softAP(settings.apSsid.c_str(), apPassword, channel, 0, settings.maxClients);

    // Work around Android devices that stall on the ESP32 access point: restart the Wi-Fi driver with A-MPDU
    // receive aggregation disabled. The driver has to be fully de-initialised for the setting to take effect, and
    // each step needs a short pause before the next one. vTaskDelay() counts FreeRTOS ticks, which are 1 ms on the
    // Arduino core, so each pause is 100 ms.
    esp_wifi_stop();

    vTaskDelay(100);
    esp_wifi_deinit();

    vTaskDelay(100);
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    config.ampdu_rx_enable = false;
    esp_wifi_init(&config);

    vTaskDelay(100);
    esp_wifi_start();

    vTaskDelay(100);
    // Re-apply both radio settings: the driver restart above may have reset them.
    applyCountry(settings);

    vTaskDelay(100);
    applyTxPower(settings);

    // The name the access point's own interface reports; in this mode the portal is reached by address, not name.
    vTaskDelay(100);
    WiFi.softAPsetHostname(settings.hostname.c_str());

    if (dnsServer == nullptr)
    {
        vTaskDelay(100);
        dnsServer = new DNSServer();
    }

    // Answer every name ("*") with the portal's address. The long TTL lets phones cache the answer, cutting the
    // number of queries the device has to serve while a page loads.
    vTaskDelay(100);
    dnsServer->setTTL(3600);
    // Port 53 is the standard DNS port, where clients send their queries.
    dnsServer->start(53, "*", localIP);

    vTaskDelay(100);
    DEBUG_SERIAL(Serial.println("================================================================================================================="));
    DEBUG_SERIAL(Serial.printf("Access point \"%s\" started at %s (channel %u, up to %u clients, country %s, %d/4 dBm)\n", settings.apSsid.c_str(), localIPHost, channel, settings.maxClients, settings.country.c_str(), settings.txPower));
    DEBUG_SERIAL(Serial.println("================================================================================================================="));
}

// Joins settings.staSsid and announces the device over mDNS. Returns false if it couldn't join in time.
static bool startStation(const DeviceSettings &settings)
{
    // There is no portal in station mode.
    if (dnsServer != nullptr)
    {
        dnsServer->stop();
        delete dnsServer;
        dnsServer = nullptr;
    }

    // The DHCP host name is sent when the interface comes up, so it must be set before WiFi.mode()/begin().
    WiFi.setHostname(settings.hostname.c_str());
    WiFi.mode(WIFI_MODE_STA);

    // The country is set before connecting, so the network is searched for on that region's channels. Transmit power
    // is set once connected: the driver refuses it until the station interface has finished starting, which
    // WiFi.mode() doesn't wait for.
    applyCountry(settings);

    WiFi.begin(settings.staSsid.c_str(), settings.staPassword.isEmpty() ? nullptr : settings.staPassword.c_str());

    // An empty saved password means an open network, which is joined by passing no password (nullptr) at all.
    // Block start-up until connected or timed out; nothing else can usefully run without a network. delay() lets the
    // Wi-Fi task run while this one waits.
    DEBUG_SERIAL(Serial.printf("Joining \"%s\"", settings.staSsid.c_str()));
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < STATION_CONNECT_TIMEOUT_MS)
    {
        DEBUG_SERIAL(Serial.print('.'));
        delay(500);
    }
    DEBUG_SERIAL(Serial.println());

    if (WiFi.status() != WL_CONNECTED)
    {
        DEBUG_SERIAL(Serial.printf("Couldn't join \"%s\"\n", settings.staSsid.c_str()));
        WiFi.disconnect(true); // also turns the station interface off before the access point starts
        return false;
    }

    applyTxPower(settings);

    // Multicast DNS makes the device reachable as http://<hostname>.local/ without knowing its DHCP address. The
    // service record also lets network browsers (Bonjour, Avahi) list it as a web server. A failure isn't fatal: the
    // device still answers at its IP address.
    if (MDNS.begin(settings.hostname.c_str()))
        MDNS.addService("http", "tcp", 80);

    DEBUG_SERIAL(Serial.printf("Joined \"%s\" as %s (http://%s.local/, country %s, %d/4 dBm)\n",
                               settings.staSsid.c_str(), WiFi.localIP().toString().c_str(),
                               settings.hostname.c_str(), settings.country.c_str(), settings.txPower));
    return true;
}

WifiMode startNetwork(const DeviceSettings &settings)
{
    // Station mode is tried first when chosen; access point mode is both the default and the fallback, so every path
    // ends with the device reachable one way or the other. runningMode records which way, for the rest of the program.
    if (settings.mode == WifiMode::Station)
    {
        if (startStation(settings))
            return runningMode = WifiMode::Station;
        DEBUG_SERIAL(Serial.println("Falling back to access point mode"));
    }

    startAccessPoint(settings);
    return runningMode = WifiMode::AccessPoint;
}

bool isAccessPointMode()
{
    return runningMode == WifiMode::AccessPoint;
}

void serviceNetwork()
{
    // DNSServer polls its UDP socket: each call answers at most one waiting query. Queries that arrive faster than
    // loop() polls wait in the socket's queue, which is why the DNS interval affects how fast pages load.
    if (runningMode == WifiMode::AccessPoint && dnsServer != nullptr)
        dnsServer->processNextRequest();
}
