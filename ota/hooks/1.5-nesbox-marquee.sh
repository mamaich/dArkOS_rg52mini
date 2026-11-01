#!/bin/bash
# 1.5: es-theme-nes-box, the default theme, put a game's marquee (its logo, shown in the game
# list) with its centre on the screen's bottom right corner: three quarters of it were off the
# screen. It goes into the bar at the bottom, the help's bar, whose text ends well before it,
# and left of ES's clock, which ES draws at the bar's right end (UI Settings > Show Clock;
# the 12-hour "00:00 PM" starts at about 0.9 of the width).
#
# The OTA client runs it with no arguments: the installed copies of the theme, on the ROMs
# partition, which the package does not touch. The build runs it on its own copies:
# 1.5-nesbox-marquee.sh <theme dir>... A main.xml whose marquee is not upstream's (the user
# changed it, or the theme did) is left alone. Safe to run twice, never fails. Not listed in
# merged: a rollback leaves the new place, which the old ES shows as well.
PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
[ $# -gt 0 ] || set -- /roms/themes/es-theme-nes-box /roms2/themes/es-theme-nes-box
for T in "$@"; do
  [ -f "$T/main.xml" ] || continue
  r=$(python3 - "$T/main.xml" <<'PY'
import sys
OLD = """      <image name="md_marquee">
         <pos>1 1</pos>
         <origin>0.5 0.5</origin>
         <maxSize>0.1 0.1</maxSize>
"""
NEW = """      <image name="md_marquee">
         <!-- dArkOS: in the bar at the bottom, left of the clock; it was centred on the corner -->
         <pos>0.895 0.965</pos>
         <origin>1 0.5</origin>
         <maxSize>0.12 0.06</maxSize>
"""
p = sys.argv[1]
s = open(p, encoding="utf-8", newline="").read()
if NEW in s:
    print("already in the bar")
elif s.count(OLD) != 1:
    print("not upstream's, left as it is")
else:
    # in place: the file keeps its owner, mode and ACL
    open(p, "w", encoding="utf-8", newline="").write(s.replace(OLD, NEW))
    print("moved into the bar")
PY
)
  echo "  nes-box: $T/main.xml: marquee ${r:-not checked}"
done
exit 0
