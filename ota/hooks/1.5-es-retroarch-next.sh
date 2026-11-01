#!/bin/bash
# 1.5: EMULATOR SETTINGS offers "retroarch-next" (RetroArch from master, /opt/retroarch-next)
# beside "retroarch" for each system RetroArch starts with its own command line, as
# scripts/rk3562/es-retroarch-next.py puts it into the image's es_systems.cfg. A device whose
# es_systems.cfg is the user's own copy (kept by the update, the image's beside it as
# es_systems.cfg.ota-new) gets the same entries in it; the rest of the file stays. An untouched
# file is the image's already. Safe to run twice.
#
# Never fails the update. Changed files go to merged for a rollback.
PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
F=/etc/emulationstation/es_systems.cfg
[ -f "$F" ] || exit 0
[ -x /opt/retroarch-next/bin/retroarch ] || exit 0
grep -q 'name="retroarch-next"' "$F" && exit 0
python3 - "$F" <<'PY' || exit 0
import re, sys
import xml.etree.ElementTree as ET
p = sys.argv[1]
s = open(p, encoding="utf-8").read()
BLOCK = re.compile(r"[ \t]*<system>.*?</system>[ \t]*\n", re.S)
DIRECT = "/usr/local/bin/%EMULATOR% -L /home/ark/.config/%EMULATOR%/cores/%CORE%_libretro.so"
EMULATOR = re.compile(r'([ \t]*)<emulator name="retroarch">.*?</emulator>[ \t]*\n', re.S)
added = []
def system(m):
    blk = m.group(0)
    command = re.search(r"<command>(.*?)</command>", blk, re.S)
    if not command or DIRECT not in command.group(1) or 'name="retroarch-next"' in blk:
        return blk
    e = EMULATOR.search(blk)
    if not e:
        return blk
    added.append(re.search(r"<name>(.*?)</name>", blk).group(1))
    nxt = e.group(0).replace('<emulator name="retroarch">', '<emulator name="retroarch-next">', 1)
    return blk[:e.end()] + nxt + blk[e.end():]
r = BLOCK.sub(system, s)
if not added:
    sys.exit(1)
ET.fromstring(r.encode("utf-8"))
open(p, "w", encoding="utf-8").write(r)
print("  %s: retroarch-next offered for %d systems" % (p, len(added)))
PY
echo "$F" >> /var/lib/darkos-ota/merged
exit 0
