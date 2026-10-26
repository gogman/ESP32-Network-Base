#include "ApiCalls.h"

#include <esp_timer.h>
#include <stdio.h>

// Routes here are public: no authentication middleware is attached, so anything added must be safe for anyone on
// the network to read. Add routes that need the admin login in AdminPages.cpp instead.

void addApiRoutes(AsyncWebServer &server)
{
    // AsyncURIMatcher::exact() matches "/api/uptime" only. A plain string would also match anything below it, such
    // as "/api/uptime/x", because the library treats a plain path as "this path or anything under it".
    // Seconds since start-up, as plain text, for the uptime shown in every page's header. Each page fetches
    // it once and then counts locally, so pages themselves stay cacheable. esp_timer counts microseconds in 64 bits
    // and so, unlike millis(), doesn't wrap after 49 days.
    server.on(AsyncURIMatcher::exact("/api/uptime"), HTTP_GET, [](AsyncWebServerRequest *request)
              {
                  char seconds[21]; // the largest 64-bit number has 20 digits
                  snprintf(seconds, sizeof(seconds), "%llu", (unsigned long long)(esp_timer_get_time() / 1000000));
                  AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", seconds);
                  // The value changes every second, so neither the browser nor anything in between may keep a copy.
                  response->addHeader("Cache-Control", "no-store");
                  request->send(response); });
}
