// The web server: page and static file routes, the captive-portal fallback and the request activity LED.
#pragma once
#include <Arduino.h>
#include <ESPAsyncWebServer.h>

// The one web server, on port 80.
extern AsyncWebServer server;

// Registers every route and starts the server. `adminUser` and `adminPassword` are the basic authentication
// login for /admin/ and the admin APIs. Call once, after the network has started.
void setUpWebserver(AsyncWebServer &server, const String &adminUser, const String &adminPassword);

// Switches the request LED off once its blink time has passed. Call from loop().
void serviceActivityLed();
