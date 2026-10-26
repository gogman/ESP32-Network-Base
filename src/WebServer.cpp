#include "WebServer.h"
#include "AccessPointRoutes.h"
#include "AdminPages.h"
#include "ApiCalls.h"
#include "App_Config.h"
#include "NetworkServices.h"

#include <LittleFS.h>

// How a request is handled. AsyncTCP receives it on its own task and ESPAsyncWebServer parses it, then:
//  1. Server-wide middleware runs (here only the activity LED).
//  2. The routes are tried in the order they were registered; the first whose path and method match handles the
//     request, after any middleware attached to that route (the admin routes' login check).
//  3. If no route matches, the onNotFound handler runs.
// Registration order in setUpWebserver() therefore decides which route wins when two could match.

// Port 80 is plain HTTP. There is no HTTPS: the device has no certificate a browser would trust, and captive-portal
// probes are plain HTTP anyway.
AsyncWebServer server(80);

// ---------------------------------------------------------------------------------------------------------------
// Request activity LED
//
// Middleware runs when a request arrives, before its response is sent, so switching the LED off there would make
// the blink last microseconds. Instead the middleware switches it on and records the time, and serviceActivityLed()
// on the loop task switches it off REQUEST_LED_BLINK_MS later. The variables are shared between the web server
// task and the loop task, hence volatile.
// ---------------------------------------------------------------------------------------------------------------

static volatile uint32_t ledOnAt = 0;
static volatile bool ledOn = false;

class ActivityLedMiddleware : public AsyncMiddleware
{
public:
    ActivityLedMiddleware()
    {
        pinMode(REQUEST_LED_PIN, OUTPUT);
        digitalWrite(REQUEST_LED_PIN, LOW);
    }

    // Called for every request. next() hands the request on to the rest of the chain; leaving it out would stop
    // the request here, which is how the authentication middleware turns away a wrong login.
    void run(AsyncWebServerRequest *request, ArMiddlewareNext next) override
    {
        ledOnAt = millis();
        ledOn = true;
        digitalWrite(REQUEST_LED_PIN, HIGH);
        next();
    }
};

void serviceActivityLed()
{
    if (ledOn && millis() - ledOnAt >= REQUEST_LED_BLINK_MS)
    {
        ledOn = false;
        digitalWrite(REQUEST_LED_PIN, LOW);
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Captive portal
// ---------------------------------------------------------------------------------------------------------------

// In access point mode the DNS server answers every name with the portal's address, so requests meant for any web
// site arrive here. True if the request was addressed to the portal itself (Host "4.3.2.1", optionally with a
// port); false if it was meant for some other site. When joined to a network there is no DNS hijacking, and every
// request that reaches the device is meant for it.
static bool isPortalHost(AsyncWebServerRequest *request)
{
    if (!isAccessPointMode())
        return true;
    // Compare the start of the Host header, then make sure the address isn't just a prefix of a longer one
    // ("4.3.2.10"): it must be followed by nothing or by ":port".
    const String &host = request->host();
    const size_t len = sizeof(localIPHost) - 1; // without the terminating zero
    return host.startsWith(localIPHost) && (host.length() == len || host[len] == ':');
}

// ---------------------------------------------------------------------------------------------------------------
// Set-up
// ---------------------------------------------------------------------------------------------------------------

// Sends a file that pack_files.py gzipped. The browser unpacks it because of the Content-Encoding header, so the
// device never has to. The library adds that header by itself only when it falls back from a missing file to its .gz
// twin; here the .gz file is opened by name, so the header is added by hand.
static void sendGzipped(AsyncWebServerRequest *request, const char *path, const char *contentType)
{
    AsyncWebServerResponse *response = request->beginResponse(LittleFS, path, contentType, false);
    response->addHeader("Content-Encoding", "gzip");
    request->send(response);
}

void setUpWebserver(AsyncWebServer &server, const String &adminUser, const String &adminPassword)
{
    // Runs for every request, including those that end in a redirect or a 404.
    server.addMiddleware(new ActivityLedMiddleware());

    // Registered first, so the probe paths are matched before the static files and the catch-all below.
    addAccessPointRoutes(server);

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
              { sendGzipped(request, HOME_PAGE_PATH, "text/html"); });

    server.on("/css/", HTTP_GET, [](AsyncWebServerRequest *request)
              { sendGzipped(request, CSS_FILE_PATH, "text/css"); });

    // Browsers ask for the icon on every site they visit. The last argument is the Cache-Control header: max-age is
    // in seconds, so the icon is kept for about 17 hours instead of being fetched with every page.
    server.serveStatic("/favicon.ico", LittleFS, "/favicon.ico", "max-age=60000");

    addAdminRoutes(server, adminUser, adminPassword);

    addApiRoutes(server);

    // Images and other files in data/files are served at the site root. Without a default file, a directory URL
    // such as "/" isn't served from here and falls through to onNotFound.
    server.serveStatic("/", LittleFS, STATIC_FILES_DIR, "max-age=60").setDefaultFile("");

    // Everything else: redirect requests meant for other sites to the portal, and answer the rest with 404.
    server.onNotFound([](AsyncWebServerRequest *request)
                      {
                          if (isPortalHost(request))
                              request->send(404, "text/plain", "Not found");
                          else
                              redirectToPortal(request); });

    // From here on AsyncTCP accepts connections on its own task; this function returns at once.
    server.begin();
}
