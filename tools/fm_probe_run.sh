#!/bin/bash
# fm_probe run without sudo: eject -> upload -> exec -> load -> pull.
#
#   ./tools/fm_probe_run.sh
#
# Uses the SCSI generic node (/dev/sg0, reachable through the disk group)
# instead of the block device, so no root is needed. The media is ejected
# first so the iPod OS mounts its filesystem (the probe writes through the
# OS file layer), then reloaded so the host can read the artefacts with
# mtools.
set -u
cd "$(dirname "$0")/.."

F=harnesses/fm_probe/build/fm_probe.bin
OUT=ports/radioplus/re/out
TMP=/tmp/fm_probe_run
mkdir -p "$OUT" "$TMP"

find_sg() {
    for s in /sys/block/sda/device/scsi_generic/sg*; do
        [ -e "$s" ] && { basename "$s"; return; }
    done
    echo sg0
}

wait_sg() {
    for _ in $(seq 1 40); do
        [ -e "/dev/$(find_sg)" ] && return 0
        sleep 2
    done
    return 1
}

wait_media() {
    for _ in $(seq 1 30); do
        [ -e /dev/sda1 ] && return 0
        sleep 2
    done
    return 1
}

echo "==> eject media (OS mounts FS, goes home)"
SG=/dev/$(find_sg)
sg_raw -t 30 "$SG" 1b 00 00 00 02 00 >/dev/null 2>&1 || true
sleep 2

echo "==> upload fm_probe.bin"
sz=$(stat -c %s "$F")
pad=$(( (sz + 511) / 512 * 512 ))
cp "$F" "$TMP/up.bin"
[ "$pad" -gt "$sz" ] && dd if=/dev/zero bs=1 count=$((pad - sz)) >> "$TMP/up.bin" 2>/dev/null
n=$((pad / 512))
for i in $(seq 0 $((n - 1))); do
    dd if="$TMP/up.bin" of="$TMP/chunk.bin" bs=512 count=1 skip="$i" 2>/dev/null
    cur=$((153616384 + i * 512))
    ab=$(printf "%02x %02x %02x %02x" $(((cur >> 24) & 255)) $(((cur >> 16) & 255)) $(((cur >> 8) & 255)) $((cur & 255)))
    sg_raw -s 512 -i "$TMP/chunk.bin" "$SG" c6 98 01 $ab >/dev/null 2>&1
done
echo "    $sz bytes in $n chunks"

echo "==> exec (blocks until the probe returns)"
sg_raw -t 240 -r 512 "$SG" c6 98 03 09 28 00 01 >/dev/null 2>&1 || true

echo "==> waiting for the device"
wait_sg || echo "    (sg node never came back)"
echo "==> load media"
sg_raw -t 30 "/dev/$(find_sg)" 1b 00 00 00 03 00 >/dev/null 2>&1 || true
wait_media || echo "    (media never came back)"

echo "==> pull"
mtype -i /dev/sda1 ::/Data/fm_probe/fm_probe.txt > "$OUT/fm_probe.txt" 2>/dev/null || true
mtype -i /dev/sda1 ::/Data/fm_probe/os_image.bin  > "$OUT/os_image.bin"  2>/dev/null || true
ls -la "$OUT"
