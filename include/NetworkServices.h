// Wi-Fi start-up in either of the two operating modes, and the captive-portal DNS server.
//
// Access point mode: the device runs its own open network at 4.3.2.1 and answers every DNS query with its own
// address. Phones detect that their "is there internet?" probe was answered by someone else and pop up the portal.
//
// Station mode: the device joins an existing network as an ordinary client, takes an address from that network's
// DHCP server and announces itself over mDNS as http://<hostname>.local/. No DNS hijacking takes place.
#pragma once
#include <Arduino.h>

#include "DeviceSettings.h"

// Starts Wi-Fi as configured in `settings` and returns the mode that is actually running. In Station mode it waits
// up to STATION_CONNECT_TIMEOUT_MS to join settings.staSsid; if that fails (wrong password, network out of range)
// it starts the access point instead, so the device can always be reached to fix the settings.
WifiMode startNetwork(const DeviceSettings &settings);

// True while the device runs its own access point (and therefore the captive DNS server).
bool isAccessPointMode();

// Answers pending DNS queries when in access point mode; does nothing in Station mode. Call from loop().
void serviceNetwork();
