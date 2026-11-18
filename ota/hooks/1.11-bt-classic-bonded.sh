#!/bin/bash
# 1.11: BlueZ takes HID from Bluetooth gamepads that pair the old way and never bond
# (ClassicBondedOnly=false in /etc/bluetooth/input.conf). The build meant to set it since
# 2026-09-19 but skipped it silently (docs/KNOWN-ISSUES.md, "Bluetooth gamepads: paired,
# connected, no input"), so the released images have Debian's file. An untouched file is
# replaced by the image's, which has the line; a file the user edited is kept, and the line
# goes in here unless it has a ClassicBondedOnly setting of its own. bluetoothd reads it at
# its next start, after the update's reboot. Safe to run twice.
#
# Never fails the update. A changed file goes to merged for a rollback.
PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
F=/etc/bluetooth/input.conf
[ -f "$F" ] || exit 0
grep -q '^ClassicBondedOnly' "$F" && exit 0
grep -q '^\[General\]' "$F" || exit 0
sed -i 's/^\[General\]/[General]\nClassicBondedOnly=false/' "$F" || exit 0
mkdir -p /var/lib/darkos-ota
grep -qxF "$F" /var/lib/darkos-ota/merged 2>/dev/null || echo "$F" >> /var/lib/darkos-ota/merged
exit 0
