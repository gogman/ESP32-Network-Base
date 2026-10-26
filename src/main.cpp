// ESP32 Network Base application.
//
// Start-up order matters: LittleFS holds the pages, NVS holds the settings and the admin login, and the web server
// needs to know which Wi-Fi mode is running before it registers the portal routes. Starting the server last means
// its first request finds everything ready.
//
// AsyncTCP handles requests on its own task. The Arduino loop task only services the captive DNS server, the
// activity LED and delayed restarts.
//
// Background on how captive portals are detected: https://textslashplain.com/2022/06/24/captive-portals/
#include "AdminPages.h"
#include "App_Config.h"
#include "DeviceSettings.h"
#include "NetworkServices.h"
#include "SerialLog.h"
#include "WebServer.h"

#include <LittleFS.h>

// How long loop() sleeps between captive DNS polls in access point mode, from the saved settings.
static uint32_t dnsInterval = DEFAULT_DNS_INTERVAL;

// Runs once after every power-up or restart, on the Arduino loop task.
void setup()
{
    // Debug builds only. The larger transmit buffer lets start-up messages queue without blocking while the UART
    // sends them.
    DEBUG_SERIAL(Serial.setTxBufferSize(256));
    DEBUG_SERIAL(Serial.begin(115200));

    // Identify the firmware build and the chip in the log.
    DEBUG_SERIAL(Serial.println("ESP32 Network Base App, v." + String(APP_VERSION) + " compiled " + String(__DATE__) + " " + String(__TIME__) + " by Gogman"));
    DEBUG_SERIAL(Serial.printf("%s-%d\n\r", ESP.getChipModel(), ESP.getChipRevision()));

    // true formats the filesystem if it can't be mounted, which recovers blank or damaged storage (the pages then
    // need uploading again). If mounting still fails, restart rather than run without any pages.
    if (!LittleFS.begin(true))
    {
        DEBUG_SERIAL(Serial.println("LittleFS mount failed, restarting"));
        // Give the message time to leave the UART, and avoid a tight restart loop.
        delay(RESTART_DELAY_MS);
        ESP.restart();
    }

    // Settings are read once, here. Nothing changes them while running: a save on the admin pages writes NVS and
    // restarts, and the new values are picked up on the way back through setup().
    // startNetwork() falls back to the access point if it can't join the configured network.
    DeviceSettings settings = loadSettings();
    dnsInterval = settings.dnsInterval;
    startNetwork(settings);

    setUpWebserver(server, settings.adminUser, settings.adminPassword);

    DEBUG_SERIAL(Serial.println("Started"));
}

// Called over and over by the Arduino core for as long as the device runs.
void loop()
{
    // Each of these returns at once when it has nothing to do. Add your own periodic work here, keeping it short:
    // anything slow delays the captive DNS answers and the LED.
    serviceNetwork();
    serviceActivityLed();
    serviceRestart();

    // In access point mode, sleep for the DNS polling interval. In station mode there is no DNS server to poll, so
    // just yield to the Wi-Fi and web server tasks. Either way the delay matters: a loop that never sleeps keeps
    // lower-priority tasks on its core from running at all.
    if (isAccessPointMode())
        delay(dnsInterval);
    else
        delay(1);
}
