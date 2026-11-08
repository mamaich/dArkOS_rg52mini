#!/bin/bash
# 1.8 brings DDR Overclock (Options -> Tools): the 928 MHz memory loader in the internal eMMC,
# installed or removed (/usr/local/bin/ddr928-tool.sh, /usr/local/share/ddr928/; docs/HARDWARE.md,
# "The memory clock"). A new image has its entry in /roms/tools; an updated device gets it here,
# where the update brought the tool (RG52 Mini only). Safe to run twice: an entry already there,
# the user's or ours, is left alone.
#
# Never fails the update. The new file is on /roms and not listed in merged: a rollback leaves
# it, and without the tool it only says what it needs.
PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
S="/usr/local/share/ddr928/DDR Overclock.sh"
T=/roms/tools
[ -f "$S" ] || exit 0
[ -d "$T" ] || exit 0
[ -e "$T/DDR Overclock.sh" ] && exit 0
if cp "$S" "$T/DDR Overclock.sh" 2>/dev/null; then
  chown --reference="$T" "$T/DDR Overclock.sh" 2>/dev/null
  chmod 775 "$T/DDR Overclock.sh" 2>/dev/null
  echo "  /roms/tools/DDR Overclock.sh added"
fi
exit 0
