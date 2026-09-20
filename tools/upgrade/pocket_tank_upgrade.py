#!/usr/bin/env python3
"""pocket_tank_upgrade.py - the double-click upgrader, Windows and macOS.

.github/workflows/upgrader.yml bundles this with esptool and the firmware
images into ONE file per platform, using PyInstaller:

    PocketTankUpgrade.exe                      Windows
    PocketTankUpgrade.command                  macOS, inside a .zip so the
                                               executable bit survives the
                                               download

Somebody with no toolchain and no terminal downloads their file, plugs the
tank in and double-clicks it. There is ONE script for both: everything that
differs between the two is a lookup in PLATFORM below, so a change to the
upgrade itself is made once.

What it writes, and what it deliberately leaves alone:

    0x0        bootloader.bin        rewritten
    0x8000     partition-table.bin   rewritten
    0x10000    pocket_tank.bin       rewritten - the app
    -----------------------------------------------------------------
    0x9000     nvs                   UNTOUCHED - the saved tank lives here
    0xf000     phy_init              UNTOUCHED
    0x290000   model                 UNTOUCHED - the 7.56 MB brain
    0xA90000   storage               UNTOUCHED

esptool erases only the sectors it writes, so the fish, their names, the sand
dollars and everything the shop sold all survive. collect.py proves the three
images cannot reach the protected partitions before either file is ever built.

A board that has never been flashed also needs the model partition, which is
8 MB and not carried here.
"""
import json, os, sys

# the only differences between the two builds: what to call the machine, what
# a port looks like there, and how to say "run me with this port instead"
PLATFORM = {
    "win32": {
        "machine": "PC",
        "port_word": "COM port",
        "port_hint": 'If Windows shows several COM ports, pass the right one:\n'
                     '     PocketTankUpgrade.exe COM7',
    },
    "darwin": {
        "machine": "Mac",
        "port_word": "serial port",
        "port_hint": 'If your Mac shows several serial ports, pass the right one by\n'
                     '   dragging it in - the tank is usually /dev/cu.usbmodem...',
    },
}


def platform():
    """the wording for the machine this was built for"""
    key = "darwin" if sys.platform == "darwin" else "win32"
    return PLATFORM[key]


def bundled(name):
    """a file PyInstaller packed inside the bundle (or beside the script in a dev run)"""
    base = getattr(sys, "_MEIPASS", os.path.dirname(os.path.abspath(__file__)))
    path = os.path.join(base, name)
    if not os.path.isfile(path):
        raise SystemExit(f"this build is incomplete: {name} is missing from the bundle")
    return path


def pause():
    """double-clicked from Explorer or Finder, the window closes the instant we return"""
    if sys.stdin and sys.stdin.isatty():
        input("\n  Press Enter to close this window. ")


def main():
    p = platform()
    spec = json.load(open(bundled("flash.json")))
    rule = "=" * 62
    print(rule)
    print("   POCKET TANK - upgrade the app")
    print(rule)
    print(f"   firmware {spec['version']}, built {spec['built']}")
    print()
    print("   Your fish, their names, your sand dollars and everything")
    print("   you have unlocked are KEPT. Only the app is replaced.")
    print()
    print(f"   1. Plug the tank into this {p['machine']} with a USB-C cable.")
    print("   2. Leave it awake - press the side button if the screen is dark.")
    print("   3. Wait. Do not unplug it until this says DONE.")
    print()

    argv = ["--chip", spec["chip"], "--baud", "460800"]
    if len(sys.argv) > 1:                      # an explicit port, e.g. PocketTankUpgrade.exe COM7
        argv += ["--port", sys.argv[1]]
    argv += ["write_flash"]
    for part in spec["parts"]:
        argv += [part["offset"], bundled(part["file"])]

    import esptool
    try:
        esptool.main(argv)
    except (esptool.FatalError, OSError) as e:  # FatalError, or a serial error opening the port
                                               # (SerialException is an OSError); the real reason
                                               # is printed, never swallowed
        print()
        print(rule)
        print("   IT DID NOT WORK")
        print(rule)
        print(f"   {e}")
        print()
        print("   Most often this is one of:")
        print("     - the cable is a charge-only cable; use a data cable")
        print("     - the tank is asleep; press the side button and retry")
        print(f"     - another window already has the {p['port_word']} open")
        print(f"   {p['port_hint']}")
        pause()
        return 1

    print()
    print(rule)
    print("   DONE - the tank is rebooting into the new app.")
    print(rule)
    print("   Your tank is exactly as you left it.")
    pause()
    return 0


if __name__ == "__main__":
    sys.exit(main())
