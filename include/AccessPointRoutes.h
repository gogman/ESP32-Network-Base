// Captive-portal routes: the connectivity-check URLs that phones and computers probe after joining a network.
#pragma once
#include <ESPAsyncWebServer.h>

// Registers the probe routes in access point mode, so joining devices show their "sign in to network" page. Does
// nothing in Station mode. Call after startNetwork().
void addAccessPointRoutes(AsyncWebServer &server);

// Sends the browser to the portal's home page, localIPURL. Used by the captive-portal probe routes and for requests
// meant for other sites.
void redirectToPortal(AsyncWebServerRequest *request);
