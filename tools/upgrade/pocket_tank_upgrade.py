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

ONE FILE PER BOARD. There are two boards - the 1.8" AMOLED tank and the
1.54" square LCD one - and each gets its own workflow, its own release and
its own download, so work on one can never change the file somebody is
downloading for the other. This script is shared: it flashes whichever
board's payload was bundled with it, named in boards.json, and says which
that is before it writes anything.

It cannot work the board out by itself, which is why the bundle has to say:
both are an ESP32-S3 with 16 MB of flash answering the bootloader with the
same chip id and the same flash id, and reading back what is already there
would only report what it was flashed with last time - exactly the thing
that is wrong when somebody is standing there with a dark screen. If a
bundle ever does carry more than one, it asks rather than guesses; --board
<id> answers that in advance.
"""
import json, os, sys, time

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


def boards():
    """every board this bundle can flash, from boards.json"""
    return json.load(open(bundled("boards.json")))


def spec_of(board):
    """that board's flash.json, with its file names made bundle-relative"""
    spec = json.load(open(bundled(os.path.join(board["dir"], "flash.json"))))
    for part in spec["parts"]:
        part["file"] = os.path.join(board["dir"], part["file"])
    return spec


def choose(bs):
    """which tank is this? Asked once, plainly, with no default that could
    quietly flash the wrong app onto somebody's fish."""
    print("   Which tank is plugged in?")
    print()
    for i, b in enumerate(bs, 1):
        print(f"     {i}. {b['label']}")
    print()
    while True:
        try:
            answer = input(f"   Type 1-{len(bs)} and press Enter: ").strip()
        except EOFError:
            raise SystemExit("\n   No answer, and there is no safe guess - nothing was written.")
        if answer.isdigit() and 1 <= int(answer) <= len(bs):
            return bs[int(answer) - 1]
        print("   Just the number, please.")


def selfcheck(spec):
    """prove this bundle is complete, with no board anywhere near it.

    Runs for EVERY board in the bundle, so a payload that was not collected
    fails here rather than on somebody's desk.

    The payload is easy: bundled() already raises for a missing file. The one
    that bit us shipped instead - esptool keeps its stub flashers as package
    DATA (esptool/targets/stub_flasher/*/<chip>.json), which PyInstaller does
    not collect unless the build says --collect-data esptool. Without them the
    exe connects to the tank, says hello, and only THEN dies with "Stub
    flasher JSON file for esp32s3 not found". Building the StubFlasher here
    reads and decodes the same file the upgrade will need, so a bundle that
    would fail on somebody's desk fails in CI instead."""
    for part in spec["parts"]:
        bundled(part["file"])
    from esptool.loader import StubFlasher
    try:
        stub = StubFlasher(spec["chip"])
    except FileNotFoundError as e:
        raise SystemExit(f"this build is incomplete: {e}\n"
                         f"(the bundler needs --collect-data esptool)")
    if not stub.text:
        raise SystemExit(f"this build is incomplete: the {spec['chip']} stub flasher is empty")
    print(f"OK: payload complete, {spec['chip']} stub flasher loaded ({len(stub.text)} bytes)")
    return 0


def read_log(port_arg, seconds=10.0, quiet=False):
    """--log: reset the tank and print what it says on its way up.

    A tank with a dark screen has already told you why - over the USB serial
    port, in the first two seconds after a reset - and nobody who was given
    one of these has a toolchain to go and read it with. esptool brings
    pyserial along, so this file can.

    Toggling DTR/RTS the way the ROM expects is what makes the chip reset,
    so the log starts at the beginning instead of halfway through."""
    import serial
    from serial.tools import list_ports
    port = port_arg
    if not port:
        cands = [d.device for d in list_ports.comports()]
        if not cands:
            if not quiet:
                print("   No serial ports at all. Is it plugged in, with a DATA cable?")
            return 1
        port = cands[-1]                 # the newest one is nearly always the tank
        if len(cands) > 1 and not quiet:
            print(f"   {len(cands)} ports here; reading {port}. Pass another if this is wrong:")
            print(f"     {', '.join(cands)}")
    if not quiet:
        print(f"   Reading {port}. {int(seconds)} seconds. Copy ALL of this and send it over.")
    print("-" * 62)
    try:
        ser = serial.Serial(port, 115200, timeout=0.2)
    except Exception as e:
        print(f"   Could not open {port}: {e}")
        return 1
    try:
        ser.setDTR(False); ser.setRTS(True)          # EN low: hold it in reset
        time.sleep(0.12)
        ser.setRTS(False)                            # and let go: it boots from here
        ser.reset_input_buffer()
        end = time.time() + seconds
        while time.time() < end:
            chunk = ser.read(4096)
            if chunk:
                sys.stdout.write(chunk.decode("utf-8", "replace"))
                sys.stdout.flush()
    except KeyboardInterrupt:
        pass
    finally:
        ser.close()
    print()
    print("-" * 62)
    if not quiet:
        print("   That is the boot log. If it ends in a panic or keeps repeating,")
        print("   the last few lines before it repeats are the ones that matter.")
    return 0


def main():
    p = platform()
    bs = boards()
    argv_rest = sys.argv[1:]
    if argv_rest and argv_rest[0] == "--selfcheck":          # CI, never a person
        for b in bs:
            if selfcheck(spec_of(b)):
                return 1
        return 0
    if argv_rest and argv_rest[0] == "--log":                # a dark screen explains itself here
        rc = read_log(argv_rest[1] if len(argv_rest) > 1 else None)
        pause()
        return rc
    picked = None
    if len(argv_rest) >= 2 and argv_rest[0] == "--board":    # for anyone who knows which they have
        picked = next((b for b in bs if b["id"] == argv_rest[1]), None)
        if not picked:
            raise SystemExit("   No such board: " + argv_rest[1] +
                             "\n   This file carries: " + ", ".join(b["id"] for b in bs))
        argv_rest = argv_rest[2:]
    rule = "=" * 62
    print(rule)
    print("   POCKET TANK - upgrade the app")
    print(rule)
    if picked is None:
        if len(bs) == 1:
            picked = bs[0]           # a one-board bundle: nothing to ask
        else:
            picked = choose(bs)
            print()
    spec = spec_of(picked)
    print(f"   {picked['label']}")
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
    if argv_rest:                              # an explicit port, e.g. PocketTankUpgrade.exe COM7
        argv += ["--port", argv_rest[0]]
    argv += ["write_flash"]
    for part in spec["parts"]:
        argv += [part["offset"], bundled(part["file"])]

    import esptool
    try:
        esptool.main(argv)
    except FileNotFoundError as e:              # a file the BUNDLE should carry, not anything
                                                # about the tank: say so instead of sending
                                                # somebody off to change their cable
        print()
        print(rule)
        print("   THIS DOWNLOAD IS INCOMPLETE")
        print(rule)
        print(f"   {e}")
        print()
        print("   Nothing is wrong with your tank or your cable - the file you")
        print("   downloaded was built wrong. Download it again; if it still")
        print("   does this, the build itself needs fixing.")
        pause()
        return 1
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
    print()
    # and then it says what happened on the way up, without being asked.
    # A dark screen has already explained itself here; asking somebody to
    # discover a flag first is asking them to debug for me.
    print("   Here is what it says as it starts. If anything is wrong, it is")
    print("   in here - copy this window and send it.")
    print()
    read_log(argv_rest[0] if argv_rest else None, seconds=8.0, quiet=True)
    pause()
    return 0


if __name__ == "__main__":
    sys.exit(main())
