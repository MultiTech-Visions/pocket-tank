#!/usr/bin/env python3
"""collect.py - gather what the upgraders flash, and prove it is safe.

Reads the firmware build's flasher_args.json (so a partition-layout change can
never leave a stale offset baked into a bundle), copies the three images the
upgrade writes into one folder, and writes flash.json beside them for
pocket_tank_upgrade.py to read at run time.

    tools/upgrade/collect.py --build-dir firmware/build --out payload

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
    ap.add_argument("--build-dir", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--version")
    a = ap.parse_args()

    fa_path = os.path.join(a.build_dir, "flasher_args.json")
    if not os.path.isfile(fa_path):
        sys.exit(f"{fa_path}: not a firmware build dir (run idf.py build first)")
    fa = json.load(open(fa_path))

    os.makedirs(a.out, exist_ok=True)
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
        shutil.copyfile(src, os.path.join(a.out, pub))
        parts.append({"offset": ent["offset"], "file": pub, "bytes": size})

    spec = {"version": a.version or git_version(),
            "built": datetime.date.today().isoformat(),
            "chip": "esp32s3",
            "parts": parts,
            "protected": [{"name": n, "offset": hex(o), "size": hex(s)} for n, o, s in guard]}
    json.dump(spec, open(os.path.join(a.out, "flash.json"), "w"), indent=2)
    total = sum(p["bytes"] for p in parts)
    print(f"upgrader payload -> {a.out}  ({spec['version']}, {total / 1024:.0f} KB over {len(parts)} images)")
    for p in parts:
        print(f"  {p['offset']:>10}  {p['file']:<22} {p['bytes'] / 1024:7.1f} KB")
    print("  protected, never written: " + ", ".join(f"{n} @ {o:#x}" for n, o, _ in guard))


if __name__ == "__main__":
    main()
