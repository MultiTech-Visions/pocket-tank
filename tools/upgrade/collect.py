#!/usr/bin/env python3
"""collect.py - gather what the upgraders flash, and prove it is safe.

Reads the firmware build's flasher_args.json (so a partition-layout change can
never leave a stale offset baked into a bundle), copies the three images the
upgrade writes into one folder, and writes flash.json beside them for
pocket_tank_upgrade.py to read at run time.

    tools/upgrade/collect.py --build-dir firmware/build-amoled18 --out payload \
            --board amoled18 --label "1.8 inch AMOLED (the round-cornered one)"

Run once per board. Each board's images go in their own folder under --out
and are listed in boards.json, which the upgrader reads to ask which tank it
is talking to. (It cannot tell by looking: both boards are an ESP32-S3 with
16 MB of flash and answer the bootloader identically.)

The guard: every byte this will write is checked against the data partitions
in firmware/partitions.csv. If an image has grown far enough to reach `nvs`
(the saved tank) or `model` (the 7.56 MB brain), this EXITS instead of
shipping an upgrader that would quietly eat somebody's fish.
"""
import argparse, datetime, json, os, shutil, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# what must survive an upgrade, by partition name in firmware/partitions.csv
PROTECTED = ("nvs", "phy_init", "model", "storage")


def protected_spans():
    """[(name, offset, size)] for the partitions an upgrade must never touch"""
    out = []
    with open(os.path.join(ROOT, "firmware", "partitions.csv")) as f:
        for line in f:
            if line.lstrip().startswith("#"):
                continue
            cols = [c.strip() for c in line.split(",")]
            if len(cols) >= 5 and cols[0] in PROTECTED:
                out.append((cols[0], int(cols[3], 0), int(cols[4], 0)))
    missing = set(PROTECTED) - {n for n, _, _ in out}
    if missing:
        sys.exit(f"partitions.csv: no {', '.join(sorted(missing))} row - refusing to guess what to protect")
    return out


def git_version():
    return subprocess.run(["git", "-C", ROOT, "describe", "--tags", "--always", "--dirty"],
                          capture_output=True, text=True, check=True).stdout.strip()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build-dir", help="a firmware build dir (not needed with --image)")
    ap.add_argument("--out", required=True)
    ap.add_argument("--version")
    ap.add_argument("--board", default="amoled18", help="folder and id for this board's images")
    ap.add_argument("--label", help="how the upgrader names this board to a person")
    ap.add_argument("--image", help="a PREBUILT merged image to carry instead of a build dir "
                                    "(e.g. a vendor's factory firmware). Written at --offset, and "
                                    "marked as wiping the board, because a merged image does.")
    ap.add_argument("--offset", default="0x0")
    a = ap.parse_args()

    if a.image:                       # a merged image: no build dir, no partition guard to run
        out_dir = os.path.join(a.out, a.board)
        os.makedirs(out_dir, exist_ok=True)
        pub = os.path.basename(a.image)
        shutil.copyfile(a.image, os.path.join(out_dir, pub))
        size = os.path.getsize(a.image)
        spec = {"version": a.version or "vendor image", "built": datetime.date.today().isoformat(),
                "chip": "esp32s3", "wipes": True,
                "parts": [{"offset": a.offset, "file": pub, "bytes": size}]}
        json.dump(spec, open(os.path.join(out_dir, "flash.json"), "w"), indent=2)
        index_path = os.path.join(a.out, "boards.json")
        index = json.load(open(index_path)) if os.path.isfile(index_path) else []
        index = [b for b in index if b["id"] != a.board]
        index.append({"id": a.board, "label": a.label or a.board, "dir": a.board, "wipes": True})
        index.sort(key=lambda b: (b.get("wipes", False), b["id"] != "amoled18"))
        json.dump(index, open(index_path, "w"), indent=2)
        print(f"vendor image -> {out_dir}  ({pub}, {size / 1024:.0f} KB at {a.offset}, WIPES the board)")
        return

    if not a.build_dir:
        sys.exit("need --build-dir (a firmware build) or --image (a prebuilt merged image)")
    fa_path = os.path.join(a.build_dir, "flasher_args.json")
    if not os.path.isfile(fa_path):
        sys.exit(f"{fa_path}: not a firmware build dir (run idf.py build first)")
    fa = json.load(open(fa_path))

    out_dir = os.path.join(a.out, a.board)
    os.makedirs(out_dir, exist_ok=True)
    parts, guard = [], protected_spans()
    for key, pub in (("bootloader", "bootloader.bin"),
                     ("partition-table", "partition-table.bin"),
                     ("app", "pocket_tank.bin")):
        ent = fa[key]
        src = os.path.join(a.build_dir, ent["file"])
        if not os.path.isfile(src):
            sys.exit(f"missing: {src}")
        off, size = int(ent["offset"], 0), os.path.getsize(src)
        # esptool erases whole 4 KB sectors, so round the write up to one
        end = off + ((size + 0xFFF) & ~0xFFF)
        for name, p_off, p_size in guard:
            if off < p_off + p_size and end > p_off:
                sys.exit(f"REFUSING TO BUILD: {pub} at {ent['offset']} runs to {end:#x} and would "
                         f"overwrite the '{name}' partition ({p_off:#x}..{p_off + p_size:#x}). "
                         f"An upgrade must never touch it.")
        shutil.copyfile(src, os.path.join(out_dir, pub))
        parts.append({"offset": ent["offset"], "file": pub, "bytes": size})

    spec = {"version": a.version or git_version(),
            "built": datetime.date.today().isoformat(),
            "chip": "esp32s3",
            "parts": parts,
            "protected": [{"name": n, "offset": hex(o), "size": hex(s)} for n, o, s in guard]}
    json.dump(spec, open(os.path.join(out_dir, "flash.json"), "w"), indent=2)
    # the index the upgrader reads: merged, so each board's run adds itself
    index_path = os.path.join(a.out, "boards.json")
    index = json.load(open(index_path)) if os.path.isfile(index_path) else []
    index = [b for b in index if b["id"] != a.board]
    index.append({"id": a.board, "label": a.label or a.board, "dir": a.board})
    index.sort(key=lambda b: b["id"] != "amoled18")        # the default board first
    json.dump(index, open(index_path, "w"), indent=2)
    total = sum(p["bytes"] for p in parts)
    print(f"upgrader payload -> {out_dir}  ({spec['version']}, {total / 1024:.0f} KB over {len(parts)} images)")
    for p in parts:
        print(f"  {p['offset']:>10}  {p['file']:<22} {p['bytes'] / 1024:7.1f} KB")
    print("  protected, never written: " + ", ".join(f"{n} @ {o:#x}" for n, o, _ in guard))


if __name__ == "__main__":
    main()
