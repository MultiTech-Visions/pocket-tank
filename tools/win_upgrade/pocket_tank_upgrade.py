#!/usr/bin/env python3
"""pocket_tank_upgrade.py - the Windows double-click upgrader.

.github/workflows/win-upgrader.yml bundles this with esptool and the firmware
images into ONE file, PocketTankUpgrade.exe, using PyInstaller. Somebody with
no toolchain and no terminal downloads that file, plugs the tank in and
double-clicks it.

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
images cannot reach the protected partitions before this exe is ever built.

A board that has never been flashed also needs the model partition, which is
8 MB and not carried here: use the browser installer for a first install.
"""
import json, os, sys


def bundled(name):
    """a file PyInstaller packed inside the exe (or beside the script in a dev run)"""
    base = getattr(sys, "_MEIPASS", os.path.dirname(os.path.abspath(__file__)))
    path = os.path.join(base, name)
    if not os.path.isfile(path):
        raise SystemExit(f"this build is incomplete: {name} is missing from the exe")
    return path


def pause():
    """double-clicked from Explorer, the window closes the instant we return"""
    if sys.stdin and sys.stdin.isatty():
        input("\n  Press Enter to close this window. ")


def main():
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
    print("   1. Plug the tank into this PC with a USB-C cable.")
    print("   2. Leave it awake - press the side button if the screen is dark.")
    print("   3. Wait. Do not unplug it until this says DONE.")
    print()

    argv = ["--chip", spec["chip"], "--baud", "460800"]
    if len(sys.argv) > 1:                      # an explicit COM port, e.g. PocketTankUpgrade.exe COM7
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
        print("     - another window already has the COM port open")
        print("   If Windows shows several COM ports, pass the right one:")
        print("     PocketTankUpgrade.exe COM7")
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
