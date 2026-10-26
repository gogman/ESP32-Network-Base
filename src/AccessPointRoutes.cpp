#include "AccessPointRoutes.h"
#include "App_Config.h"
#include "NetworkServices.h"
#include "SerialLog.h"

// How a captive portal works, step by step:
//  1. A phone joins the device's open network. DHCP gives it 4.3.2.1 as both gateway and DNS server.
//  2. The phone asks DNS for its vendor's probe host (connectivitycheck.gstatic.com, captive.apple.com, ...). The
//     captive DNS server in NetworkServices.cpp answers every name with 4.3.2.1, so the probe arrives here.
//  3. The phone expects a specific reply (a 204, or a page saying "Success"). It gets a redirect instead, concludes
//     that the network needs a sign-in, and opens the URL it was redirected to in a small "sign in" browser.
// The routes below handle step 3. Requests for any other site end up in WebServer.cpp's onNotFound handler, which
// redirects them the same way.

void redirectToPortal(AsyncWebServerRequest *request)
{
    DEBUG_SERIAL(Serial.println("Redirect to captive portal: " + request->url()));
    // A 302 Found with "Location: http://4.3.2.1/". The full URL, not just "/", matters: the request was addressed to
    // another host name, and a relative redirect would keep the browser on that (hijacked) name.
    request->redirect(localIPURL);
}

void addAccessPointRoutes(AsyncWebServer &server)
{
    // In station mode none of these routes exist, and the device behaves like an ordinary web server.
    //
    // Notes for anyone editing the portal pages:
    //  - iOS doesn't show the portal if the portal page itself contains the word "Success".
    //  - The iOS portal window caps an HTML page at 128 KB (linked resources don't count) and runs with
    //    JavaScript and cookies disabled, so portal pages must work as plain HTML.
    if (!isAccessPointMode())
        return;

    // Paths that phones and computers fetch to test for internet access. Answering them with a redirect, instead of
    // the response the operating system expects, is what makes it show the "sign in to network" portal:
    //   /generate_204         Android and Chrome OS (expects 204 No Content)
    //   /hotspot-detect.html  Apple iOS and macOS (expects a page containing "Success")
    //   /canonical.html       Firefox
    //   /ncsi.txt, /redirect  Windows Network Connectivity Status Indicator
    // The host name in the probe doesn't matter: DNS sent every name here, so matching on the path alone is enough.
    static constexpr const char *kProbePaths[] = {"/generate_204", "/hotspot-detect.html", "/canonical.html", "/ncsi.txt", "/redirect"};
    for (const char *path : kProbePaths)
        server.on(path, redirectToPortal);

    // Windows 10/11 connectivity probe. Redirecting it to an outside address (rather than to the portal) is a known
    // workaround that makes Windows open its sign-in browser.
    server.on("/connecttest.txt", [](AsyncWebServerRequest *request)
              { request->redirect("http://logout.net"); });

    // Windows looks for a proxy auto-configuration file and retries rapidly, loading the device, unless it gets a
    // definite 404.
    server.on("/wpad.dat", [](AsyncWebServerRequest *request)
              { request->send(404); });

    // Firefox also requests /success.txt; it is answered with an empty 200 response.
    server.on("/success.txt", [](AsyncWebServerRequest *request)
              { request->send(200); });
}
