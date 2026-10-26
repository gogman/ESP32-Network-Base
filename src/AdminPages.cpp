#include "AdminPages.h"
#include "App_Config.h"
#include "DeviceSettings.h"
#include "NetworkServices.h"
#include "SerialLog.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <WiFi.h>

// How an admin page works, from the browser's side:
//  1. GET /admin/ (or /admin/network/). The authentication middleware finds no login and answers 401 with a
//     "WWW-Authenticate: Basic" header; the browser shows its login box and repeats the request with the login.
//  2. The page arrives with an empty, disabled form. Its script fetches GET /api/admin/ (or /api/network/), which
//     returns the current settings as JSON, fills the form and enables it. The browser sends the remembered login
//     with every later request to the same site, so the script never handles the password itself.
//  3. Submitting posts the form, url-encoded, to the same API path. The handler starts from the saved settings,
//     overwrites only the fields that page owns, validates and saves them, replies {"message": ...} and schedules
//     a restart. On a problem it replies {"error": ...} with status 400 (bad input) or 500 (NVS write failed) and
//     the page re-enables the form so the user can correct it.
//
// All handlers run on the AsyncTCP task, not the Arduino loop task. They must not block: no delay(), no waiting for
// Wi-Fi. That is why a save only schedules the restart, and serviceRestart() on the loop task carries it out.

// One middleware instance shared by every admin route, so they all check the same login. It runs before the route's
// handler: if the request carries the right login it passes the request on, otherwise it answers 401 itself and the
// handler never runs.
static AsyncAuthenticationMiddleware basicAuth;

// A save can't restart the device from inside the request handler: the reply would never be sent.
// The handler only records the request; serviceRestart() on the loop task restarts once RESTART_DELAY_MS passed.
// Both variables are written by the web server task and read by the loop task, hence volatile.
static volatile bool restartPending = false;
static volatile uint32_t restartRequestedAt = 0;

void serviceRestart()
{
    // Unsigned subtraction keeps working when millis() wraps around after about 49 days.
    if (restartPending && millis() - restartRequestedAt >= RESTART_DELAY_MS)
    {
        DEBUG_SERIAL(Serial.println("Restarting to apply settings..."));
        DEBUG_SERIAL(Serial.flush());
        ESP.restart();
    }
}

// Sends `doc` as the JSON reply. Settings change, so the reply is never cached.
static void sendJson(AsyncWebServerRequest *request, const JsonDocument &doc, int status = 200)
{
    String body;
    serializeJson(doc, body);
    AsyncWebServerResponse *response = request->beginResponse(status, "application/json", body);
    response->addHeader("Cache-Control", "no-store");
    request->send(response);
}

// Replies {"error": error} with the given HTTP status. The pages show the text as it is.
static void sendError(AsyncWebServerRequest *request, int status, const String &error)
{
    JsonDocument doc;
    doc["error"] = error;
    sendJson(request, doc, status);
}

// One line describing the network the device is on right now, which may differ from the saved settings (for
// example after falling back to the access point, or before a saved change has been applied by a restart).
static String statusLine()
{
    if (isAccessPointMode())
        return "Running as access point \"" + WiFi.softAPSSID() + "\" at " + WiFi.softAPIP().toString() + ".";
    return "Joined \"" + WiFi.SSID() + "\" at " + WiFi.localIP().toString() + ".";
}

// Value of a form field from the POST body, or an empty string if the field wasn't sent.
static String postValue(AsyncWebServerRequest *request, const char *name)
{
    // `true` looks in the POST body rather than the query string, so a field can't be slipped in through the URL.
    const AsyncWebParameter *p = request->getParam(name, true);
    return p ? p->value() : String();
}

// A numeric form field as a byte. Missing, non-numeric or out-of-byte-range input becomes 0, which every numeric
// setting rejects, so validateSettings() reports it instead of a wrapped-around value slipping through.
static uint8_t postByte(AsyncWebServerRequest *request, const char *name)
{
    long value = postValue(request, name).toInt();
    return value > 0 && value <= 255 ? (uint8_t)value : 0;
}

// Reads a new password and its confirmation into `password`. A blank field keeps the saved password. Returns false,
// having replied with an error, if the two fields don't match. The pages check this before submitting, but only with
// JavaScript running, so it is repeated here. Passwords aren't trimmed, since spaces are valid in them.
static bool readNewPassword(AsyncWebServerRequest *request, const char *field, const char *confirmField, String &password)
{
    String value = postValue(request, field);
    if (value != postValue(request, confirmField))
    {
        sendError(request, 400, "The passwords don't match.");
        return false;
    }
    if (value.length())
        password = value;
    return true;
}

// The end of every save: validate, save, confirm with `message`, then restart so the new settings take effect.
static void finishSave(AsyncWebServerRequest *request, const DeviceSettings &settings, const String &message)
{
    String error = validateSettings(settings);
    if (error.length())
    {
        sendError(request, 400, error);
        return;
    }
    if (!saveSettings(settings))
    {
        sendError(request, 500, "The settings couldn't be saved. Please try again.");
        return;
    }

    JsonDocument doc;
    doc["message"] = message;
    sendJson(request, doc);

    // send() only queues the reply; AsyncTCP transmits it after this handler returns. The restart therefore waits
    // RESTART_DELAY_MS, long enough for the reply to arrive and the page to show it. The time is stored before the
    // flag, so the loop task never sees the flag set with a stale time.
    restartRequestedAt = millis();
    restartPending = true;
}

// GET /api/admin/: the device settings shown on the admin page.
static void handleGetAdmin(AsyncWebServerRequest *request)
{
    DeviceSettings settings = loadSettings();
    JsonDocument doc;
    doc["status"] = statusLine();
    doc["device_name"] = settings.hostname;
    doc["admin_user"] = settings.adminUser;
    sendJson(request, doc);
}

// POST /api/admin/: device name and admin login.
static void handleSaveAdmin(AsyncWebServerRequest *request)
{
    // Start from the saved settings, so the network settings and the stored password are kept.
    DeviceSettings settings = loadSettings();
    settings.hostname = postValue(request, "device_name");
    settings.hostname.trim();

    String oldUser = settings.adminUser;
    settings.adminUser = postValue(request, "admin_user");
    settings.adminUser.trim();

    String oldPassword = settings.adminPassword;
    if (!readNewPassword(request, "admin_pass", "admin_pass_confirm", settings.adminPassword))
        return;

    // Build the confirmation. A new host name only matters in station mode, where it is the .local address; and if
    // the login changed, the browser's remembered one stops working after the restart, so say so.
    String message = "Saved. The device is restarting";
    if (!isAccessPointMode())
        message += " and will answer at http://" + settings.hostname + ".local/";
    message += ".";
    if (settings.adminUser != oldUser || settings.adminPassword != oldPassword)
        message += " Log in with the new login.";
    finishSave(request, settings, message);
}

// GET /api/network/: the network settings, plus every choice the form offers, so the page builds its drop-downs and
// limits from the same lists validateSettings() checks against.
static void handleGetNetwork(AsyncWebServerRequest *request)
{
    DeviceSettings settings = loadSettings();
    JsonDocument doc;
    doc["status"] = statusLine();

    // Keys are the form field names, so the page can fill the form by name.
    JsonObject values = doc["values"].to<JsonObject>();
    values["mode"] = settings.mode == WifiMode::Station ? "sta" : "ap";
    values["sta_ssid"] = settings.staSsid;
    values["ap_ssid"] = settings.apSsid;
    values["wifi_channel"] = settings.wifiChannel;
    values["max_clients"] = settings.maxClients;
    values["dns_interval"] = settings.dnsInterval;
    values["country"] = settings.country;
    values["tx_power"] = settings.txPower;

    // Never send the password back; the page only needs to know whether one is saved.
    doc["sta_pass_set"] = !settings.staPassword.isEmpty();

    // Each limit is a [min, max] pair, which the page copies into the number inputs' min and max attributes.
    JsonObject limits = doc["limits"].to<JsonObject>();
    limits["wifi_channel"][0] = WIFI_CHANNEL_MIN;
    limits["wifi_channel"][1] = WIFI_CHANNEL_MAX;
    limits["max_clients"][0] = MAX_CLIENTS_MIN;
    limits["max_clients"][1] = MAX_CLIENTS_MAX;
    limits["dns_interval"][0] = DNS_INTERVAL_MIN;
    limits["dns_interval"][1] = DNS_INTERVAL_MAX;

    JsonArray countries = doc["countries"].to<JsonArray>();
    for (size_t i = 0; i < kWifiCountryCount; i++)
    {
        JsonObject c = countries.add<JsonObject>();
        c["code"] = kWifiCountries[i].code;
        c["name"] = kWifiCountries[i].name;
        c["max_channel"] = kWifiCountries[i].maxChannel;
    }

    JsonArray levels = doc["tx_power_levels"].to<JsonArray>();
    for (size_t i = 0; i < kTxPowerLevelCount; i++)
    {
        JsonObject level = levels.add<JsonObject>();
        level["value"] = kTxPowerLevels[i].quarterDbm;
        level["label"] = kTxPowerLevels[i].label;
    }

    sendJson(request, doc);
}

// POST /api/network/: the network settings form.
static void handleSaveNetwork(AsyncWebServerRequest *request)
{
    // Start from the saved settings, so the device name, the admin login and the stored Wi-Fi password are kept.
    DeviceSettings settings = loadSettings();
    settings.mode = postValue(request, "mode") == "sta" ? WifiMode::Station : WifiMode::AccessPoint;
    settings.apSsid = postValue(request, "ap_ssid");
    settings.staSsid = postValue(request, "sta_ssid");
    settings.maxClients = postByte(request, "max_clients");
    settings.wifiChannel = postByte(request, "wifi_channel");
    settings.dnsInterval = postByte(request, "dns_interval");
    settings.country = postValue(request, "country");

    // Transmit power can be negative (-1 dBm is -4), so it isn't read with postByte. Anything outside a signed
    // byte becomes 0, which isn't a valid level, so validateSettings() rejects it.
    long txPower = postValue(request, "tx_power").toInt();
    settings.txPower = txPower >= -128 && txPower <= 127 ? (int8_t)txPower : 0;
    settings.apSsid.trim();
    settings.staSsid.trim();

    // Password: the "Open network" checkbox clears it; otherwise a blank field keeps it and anything else replaces it.
    if (request->hasParam("sta_open", true))
        settings.staPassword = String();
    else if (!readNewPassword(request, "sta_pass", "sta_pass_confirm", settings.staPassword))
        return;

    // Tell the user where to find the device after the restart, which depends on the new mode.
    String message = settings.mode == WifiMode::Station
                         ? "Saved. The device is restarting and will join \"" + settings.staSsid + "\"; find it at http://" +
                               settings.hostname + ".local/. If it can't join, it comes back as access point \"" +
                               settings.apSsid + "\"."
                         : "Saved. The device is restarting as access point \"" + settings.apSsid +
                               "\"; reconnect to that network to continue.";
    finishSave(request, settings, message);
}

// Sends one of the gzipped admin pages from LittleFS. The library adds Content-Encoding by itself only when it falls
// back from a missing file to its .gz twin; here the .gz file is opened by name, so the header is added by hand. It
// is what tells the browser to unpack the page. `false` shows the page rather than offering it as a download.
static void sendPage(AsyncWebServerRequest *request, const char *path)
{
    AsyncWebServerResponse *response = request->beginResponse(LittleFS, path, "text/html", false);
    response->addHeader("Content-Encoding", "gzip");
    request->send(response);
}

void addAdminRoutes(AsyncWebServer &server, const String &adminUser, const String &adminPassword)
{
    basicAuth.setAuthFailureMessage("Authentication failed.");

    // loadSettings() always supplies a password, so the open branch is only a guard against an empty one: rather
    // than lock everyone out with a login nobody can type, the admin area is left open.
    if (adminPassword.length())
    {
        basicAuth.setRealm(ADMIN_REALM);
        basicAuth.setUsername(adminUser.c_str());
        basicAuth.setPassword(adminPassword.c_str());
        basicAuth.setAuthType(AsyncAuthType::AUTH_BASIC);
    }
    else
    {
        DEBUG_SERIAL(Serial.println("WebServer: no admin password, so leave admin open"));
        basicAuth.setAuthType(AsyncAuthType::AUTH_NONE);
    }

    // The login is fixed here, at start-up. A changed login takes effect through the restart that follows every
    // save, so the middleware never has to be updated while running.

    // The middleware is added to each route rather than to the server, so the public pages stay open.
    server.on("/admin/", HTTP_GET, [](AsyncWebServerRequest *request)
              { sendPage(request, ADMIN_PAGE_PATH); })
        .addMiddleware(&basicAuth);

    server.on("/admin/network/", HTTP_GET, [](AsyncWebServerRequest *request)
              { sendPage(request, NETWORK_PAGE_PATH); })
        .addMiddleware(&basicAuth);

    // exact() so that only these paths match, not anything below them. GET and POST on the same path are separate
    // routes, each with its own handler, and both need the login.
    server.on(AsyncURIMatcher::exact("/api/admin/"), HTTP_GET, handleGetAdmin).addMiddleware(&basicAuth);
    server.on(AsyncURIMatcher::exact("/api/admin/"), HTTP_POST, handleSaveAdmin).addMiddleware(&basicAuth);
    server.on(AsyncURIMatcher::exact("/api/network/"), HTTP_GET, handleGetNetwork).addMiddleware(&basicAuth);
    server.on(AsyncURIMatcher::exact("/api/network/"), HTTP_POST, handleSaveNetwork).addMiddleware(&basicAuth);

    // Without the trailing slash, and the setup page's old address.
    server.on("/admin", HTTP_GET, [](AsyncWebServerRequest *request)
              { request->redirect("/admin/"); });

    server.on("/admin/setup", HTTP_GET, [](AsyncWebServerRequest *request)
              { request->redirect("/admin/network/"); });
}
