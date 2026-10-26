"""
Compresses the web pages before every build.

PlatformIO runs this script itself, before compiling (extra_scripts in
platformio.ini). For each file in PAGES it reads web/<file> and writes
data/<file>.gz, the copy that `pio run -t uploadfs` puts on LittleFS and the
device sends to the browser. The .gz files are generated, so they are not
kept in version control; edit the files in web/ only.

A file is only rewritten when its content changes, so its timestamp shows
when the page last really changed.
"""

import gzip
import pathlib

Import("env")  # noqa: F821 - provided by PlatformIO's build system

# env.subst() expands PlatformIO's build variables; $PROJECT_DIR is the folder holding platformio.ini.
PROJECT = pathlib.Path(env.subst("$PROJECT_DIR"))
SOURCE = PROJECT / "web"
TARGET = PROJECT / "data"

# Files to pack. Add a new page here, and its .gz path to App_Config.h, or it never reaches the device.
PAGES = ["index.html", "admin.html", "network.html", "bootstrap.css"]

print("******************************************************************************")
print("Compressing and moving web pages to data/ for ESP32-Network-Base firmware")
print("Project directory: %s" % PROJECT)
print("Compressing %s to %s..." % (SOURCE.relative_to(PROJECT), TARGET.relative_to(PROJECT)))
print("******************************************************************************")

for page in PAGES:
    source = SOURCE / page
    target = TARGET / (page + ".gz")

    # compresslevel=9 is the smallest output: compression happens once on the PC, so its speed doesn't matter, and
    # smaller files load faster over the device's Wi-Fi. mtime=0 leaves the time of compression out of the file, so the same page
    # always gives the same bytes and an unchanged page isn't rewritten.
    packed = gzip.compress(source.read_bytes(), compresslevel=9, mtime=0)

    if not target.exists() or target.read_bytes() != packed:
        target.write_bytes(packed)

    print("Page: %s, %d bytes, packed: %d bytes" % (source.name, source.stat().st_size, len(packed)))

print("******************************************************************************")
