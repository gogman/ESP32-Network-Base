# ESP32 Network Base

A starting point for ESP32 projects that need Wi-Fi and a web interface. Out of the box the device:

- runs its own open Wi-Fi network with a **captive portal**, so phones and laptops that join it are taken
  straight to the device's home page, or
- joins an existing Wi-Fi network as an ordinary client and can be reached at `http://<hostname>.local/`;
- serves a small Bootstrap web site from LittleFS, with a password-protected **admin area** for changing the
  device name, the admin password and every network setting;
- keeps its settings in NVS, so they survive firmware and filesystem uploads.

Add your own pages, routes and hardware code on top of it.

## Hardware and tools

- An ESP32 with 4 MB flash. The project is set up for the Freenove ESP32-WROVER board; change `board` in
  [platformio.ini](platformio.ini) for others.
- [PlatformIO](https://platformio.org/) (VS Code extension or CLI). Run `pio` from PowerShell or cmd, not Git Bash.
- `upload_port` and `monitor_port` in [platformio.ini](platformio.ini) are set to `COM20`. Change them to your board's port.

## Getting started

```
pio run -e Release -t upload      # build and upload the firmware
pio run -e Release -t uploadfs    # upload the web pages to LittleFS
pio device monitor                # serial log (Debug builds only)
```

Both uploads are needed the first time. After that, upload the firmware when the C++ changes and the filesystem
when anything in `web/` changes.

On first start-up the device runs as an access point named **ESP32 Network Base XXXXXX**, where XXXXXX is six
random hexadecimal digits, so neighbouring devices get different names. Join that network and the sign-in page
should open by itself. If it doesn't, browse to <http://4.3.2.1/>.

### Admin area

Open **Device Administration** on the home page, or go to `/admin/`. The default login is **admin** / **password**.
Change it on first use.

| Page | What it sets |
|---|---|
| `/admin/` | Device name (used as the host name) and the admin user name and password |
| `/admin/network/` | Wi-Fi mode, the network to join and its password, the access point's name, channel and maximum clients, the DNS polling interval, the country and the transmit power |

Saving checks the values, stores them and restarts the device so they take effect.

If the device can't join the configured network within 20 seconds, it starts the access point instead, so you
can always reach it to fix the settings.

## Build environments

| Environment | Use |
|---|---|
| `Release` | Normal builds. No serial output. |
| `Debug` | Serial logging at 115200 baud (`APP_SERIAL_LOGGING`), verbose ESP-IDF logs, and crash backtraces decoded into source lines. |

Serial logging goes through the `DEBUG_SERIAL(...)` macro in [SerialLog.h](include/SerialLog.h), which compiles
to nothing in Release builds.

## Project layout

| Path | Contents |
|---|---|
| [src/main.cpp](src/main.cpp) | Start-up order and the main loop: captive DNS, activity LED, delayed restart |
| [src/NetworkServices.cpp](src/NetworkServices.cpp) | Access point and station start-up, country and transmit power, captive DNS, mDNS |
| [src/WebServer.cpp](src/WebServer.cpp) | Registers every route, serves the pages and static files, and blinks the activity LED on each request |
| [src/AccessPointRoutes.cpp](src/AccessPointRoutes.cpp) | The connectivity-check URLs that phones and computers probe, redirected to the portal |
| [src/AdminPages.cpp](src/AdminPages.cpp) | The admin pages and their JSON settings API, behind basic authentication |
| [src/ApiCalls.cpp](src/ApiCalls.cpp) | Public API: `/api/uptime` |
| [src/DeviceSettings.cpp](src/DeviceSettings.cpp) | Loading, validating and saving the settings in NVS; the country and transmit power lists |
| [include/App_Config.h](include/App_Config.h) | Compile-time configuration: file paths, defaults and limits, LED pin, portal address |
| [web/](web/) | Source of the web pages and stylesheet. Edit these files |
| [data/](data/) | What `uploadfs` puts on LittleFS. The `.gz` files are generated, so don't edit them |
| [scripts/pack_files.py](scripts/pack_files.py) | Gzips the files in `web/` into `data/` before every build |
| [defpart.csv](defpart.csv) | Flash layout: NVS, about 1.9 MB of firmware and about 1.9 MB of LittleFS |

## Web interface

Pages are plain HTML with Bootstrap 5 and a light/dark theme switch. The browser fetches each admin page's
settings from the device as JSON and saves them back with a form post.

| Route | Access | Purpose |
|---|---|---|
| `/` | Public | Home page |
| `/css/` | Public | Stylesheet |
| `/api/uptime` | Public | Seconds since start-up, as plain text |
| `/admin/`, `/admin/network/` | Admin login | Admin pages |
| `GET /api/admin/`, `GET /api/network/` | Admin login | Current settings and the choices each form offers (passwords are never sent) |
| `POST /api/admin/`, `POST /api/network/` | Admin login | Save the form. The reply is `{"message"}` on success, after which the device restarts, or `{"error"}` with status 400 or 500 |

Other files in `data/files/` are served from the site root, so `data/files/logo.png` becomes `/logo.png`.

### Adding a page

1. Add the HTML file to `web/` and its name to `PAGES` in [scripts/pack_files.py](scripts/pack_files.py).
2. Add its path, for example `/mypage.html.gz`, to [App_Config.h](include/App_Config.h) and register a route
   for it in [WebServer.cpp](src/WebServer.cpp), or in [AdminPages.cpp](src/AdminPages.cpp) if it needs the admin
   login. Copy the existing routes, which send the gzipped file with `Content-Encoding: gzip`.
3. Build, then upload both the firmware and the filesystem.

## Captive portal

In access point mode the device's address is **4.3.2.1**, and its DNS server answers every name with that
address. Phones and computers check for internet access by fetching known URLs such as `/generate_204`
(Android) or `/hotspot-detect.html` (Apple). The device redirects those to the portal, which makes the operating
system show its "sign in to network" page. Requests addressed to any other web site are redirected the same way.

The address is deliberately outside the private ranges: some Android phones, notably Samsung's, won't show the
sign-in page for a portal on a private network such as 192.168.x.x.

Background on how captive portals are detected: <https://textslashplain.com/2022/06/24/captive-portals/>

## Configuration

Most things you'd want to change are in [App_Config.h](include/App_Config.h), each with a comment:

- the default admin login
- the GPIO of the request activity LED and how long it blinks (GPIO 2, the on-board LED, by default)
- the default and allowed ranges for maximum clients, Wi-Fi channel and DNS polling interval
- the default country and transmit power
- the access point password (`nullptr` makes it an open network)
- how long to try joining a network before falling back to the access point

Values set on the admin pages are stored in NVS and take precedence over the defaults. To return to the defaults,
erase the flash (`pio run -t erase`), then upload the firmware and the filesystem again.

## Libraries

- [ESPAsyncWebServer](https://github.com/ESP32Async/ESPAsyncWebServer) and [AsyncTCP](https://github.com/ESP32Async/AsyncTCP)
- [ArduinoJson](https://arduinojson.org/) 7
- [Bootstrap](https://getbootstrap.com/) 5, bundled in [web/bootstrap.css](web/bootstrap.css)

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE).
