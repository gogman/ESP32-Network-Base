// The password-protected admin area: /admin/ (device name and admin login) and /admin/network/ (Wi-Fi settings).
#pragma once
#include <ESPAsyncWebServer.h>

// Registers the admin pages and their settings APIs, all behind basic authentication with the given login:
//   GET  /api/admin/, /api/network/   the page's current settings (never passwords) and choices, as JSON
//   POST /api/admin/, /api/network/   the page's form fields, url-encoded; replies {"message"} on success, then
//                                     restarts, or {"error"} with status 400/500
void addAdminRoutes(AsyncWebServer &server, const String &adminUser, const String &adminPassword);

// Restarts the device once a restart requested by a successful save is due. Call from loop().
void serviceRestart();
