#!/bin/bash
# 1.4: standalone DuckStation is no longer in the image (its author does not agree to the way
# dArkOS passed it on; docs/KNOWN-ISSUES.md, DuckStation). The update deletes the files of it
# the old image had and the user did not change; PS1 games run in RetroArch. This takes the
# emulator out of what would still start it:
# - an es_systems.cfg that is the user's own copy (kept by the update, the image's beside it
#   as es_systems.cfg.ota-new) loses its standalone-duckstation entry; an untouched file is
#   the image's already. A system's choice of it needs nothing: ES takes the system's first
#   emulator for a name it does not list;
# - a game set to standalone-duckstation in ES (its gamelist.xml, in the ROM folder or in
#   ~/.emulationstation/gamelists) loses that setting, so ES starts it with the system's.
# DuckStation's memory cards and settings in ~/.local/share/duckstation stay.
#
# Never fails the update. Changed files on the root filesystem go to merged for a rollback;
# one on /roms is left out, as the rollback would delete a path its snapshot has not got.
PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
merged() {
  [ "$(findmnt -n -o TARGET -T "$1" 2>/dev/null)" = / ] && echo "$1" >> /var/lib/darkos-ota/merged
}
F=/etc/emulationstation/es_systems.cfg
if [ -f "$F" ] && grep -q '<emulator name="standalone-duckstation"' "$F" && python3 - "$F" <<'PY'
import re, sys
import xml.etree.ElementTree as ET
p = sys.argv[1]
s = open(p, encoding="utf-8").read()
r = re.sub(r'\n[ \t]*<emulator name="standalone-duckstation"\s*(/>|>.*?</emulator>)', "", s, flags=re.S)
if r == s:
    sys.exit(1)
ET.fromstring(r.encode("utf-8"))
open(p, "w", encoding="utf-8").write(r)
PY
then
  merged "$F"
  echo "  $F: standalone-duckstation taken out"
fi
for G in /roms/*/gamelist.xml /roms2/*/gamelist.xml /home/ark/.emulationstation/gamelists/*/gamelist.xml; do
  [ -f "$G" ] && grep -q '<emulator>standalone-duckstation</emulator>' "$G" || continue
  sed -i '/^[[:space:]]*<emulator>standalone-duckstation<\/emulator>[[:space:]]*$/d' "$G" || continue
  merged "$G"
  echo "  $G: games set to standalone-duckstation go back to the system's emulator"
done
exit 0
