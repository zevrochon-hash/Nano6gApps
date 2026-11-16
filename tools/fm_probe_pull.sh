#!/bin/bash
# Run the fm_probe harness on the iPod, pull its artefacts, and analyse them.
#
#   ./tools/fm_probe_pull.sh
#
# Requires the iPod to be attached (local transport, or the SSH transport the
# `start` tool is configured for). Produces:
#
#   ports/radioplus/re/out/fm_probe.txt    the probe report
#   ports/radioplus/re/out/os_image.bin    the live OS image
#   and prints the analysis to stdout.
#
# Untested until a device is attached — the mount step mirrors the `start`
# tool's own _mount_pull path.
set -e
cd "$(dirname "$0")/.."

ROOT=$(pwd)
MNT=${IPOD_MOUNT:-/mnt/nanoapps-ipod}
TMP=/tmp/nano_fm_probe
OUT=ports/radioplus/re/out

echo "==> running fm_probe on the device"
./start run fm_probe

echo "==> mounting the iPod to pull the artefacts"
DEV=${IPOD_DEV:-$(lsblk -dno NAME,MODEL 2>/dev/null | awk 'tolower($0) ~ /ipod/ {print "/dev/"$1; exit}')}
[ -n "$DEV" ] || { echo "no iPod found (set IPOD_DEV)"; exit 1; }
PART=${IPOD_PARTITION:-${DEV}1}

sudo sg_start "$DEV" --loej 2>/dev/null || true
for i in $(seq 1 30); do test -e "$PART" && break; sleep 1; done
test -e "$PART" || { echo "partition $PART never appeared"; exit 1; }
sudo fsck.vfat -a "$PART" >/dev/null 2>&1 || true
sudo mkdir -p "$MNT"
sudo umount "$PART" 2>/dev/null || true
ok=
for i in $(seq 1 20); do
    sudo mount -o ro,flush "$PART" "$MNT" 2>/dev/null && { ok=1; break; }
    sleep 1
done
[ -n "$ok" ] || { echo "could not mount $PART"; exit 1; }

rm -rf "$TMP"; mkdir -p "$TMP"
cp "$MNT/Data/fm_probe/fm_probe.txt" "$TMP/" 2>/dev/null || true
cp "$MNT/Data/fm_probe/os_image.bin"  "$TMP/" 2>/dev/null || true
sudo umount "$MNT" 2>/dev/null || true

mkdir -p "$OUT"
cp "$TMP"/* "$OUT"/ 2>/dev/null || true

if [ -f "$OUT/fm_probe.txt" ]; then
    echo "==> analysing"
    if [ -f "$OUT/os_image.bin" ]; then
        python3 tools/fm_analyze.py "$OUT/fm_probe.txt" "$OUT/os_image.bin"
    else
        echo "(no os_image.bin — report only)"
        python3 tools/fm_analyze.py "$OUT/fm_probe.txt"
    fi
else
    echo "no report pulled — did the probe run? (check /Data/fm_probe on the iPod)"
fi
