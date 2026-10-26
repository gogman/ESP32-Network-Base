// Public JSON and text API, available without logging in. The admin settings API is in AdminPages.h.
#pragma once
#include <ESPAsyncWebServer.h>

// Registers the public API routes:
//   GET /api/uptime   seconds since start-up, as plain text
void addApiRoutes(AsyncWebServer &server);
