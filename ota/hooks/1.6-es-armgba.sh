#!/bin/bash
# 1.6: EMULATOR SETTINGS offers armGBA for GBA (since 1.5), after mGBA, in the "retroarch" and
# "retroarch-next" emulators. The update to 1.5 put it only into the image's es_systems.cfg; a
# device whose es_systems.cfg is the user's own copy (kept by the update, the image's beside it
# as es_systems.cfg.ota-new) gets the same core in it here; the rest of the file stays. An
# untouched file is the image's already. Runs after 1.5-es-retroarch-next.sh in a package that
# has both. Safe to run twice.
#
# Never fails the update. Changed files go to merged for a rollback.
PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
F=/etc/emulationstation/es_systems.cfg
[ -f "$F" ] || exit 0
[ -f /home/ark/.config/retroarch/cores/armgba_libretro.so ] || exit 0
python3 - "$F" <<'PY' || exit 0
import re, sys
import xml.etree.ElementTree as ET
p = sys.argv[1]
s = open(p, encoding="utf-8").read()
BLOCK = re.compile(r"[ \t]*<system>.*?</system>[ \t]*\n", re.S)
EMULATOR = re.compile(r'[ \t]*<emulator name="(retroarch|retroarch-next)">.*?</emulator>[ \t]*\n', re.S)
MGBA = re.compile(r"([ \t]*)<core>mgba</core>[ \t]*\n")
added = []
def emulator(m):
    e = m.group(0)
    if "<core>armgba</core>" in e:
        return e
    c = MGBA.search(e)
    if not c:
        return e
    added.append(m.group(1))
    return e[:c.end()] + c.group(1) + "<core>armgba</core>\n" + e[c.end():]
def system(m):
    blk = m.group(0)
    if not re.search(r"<name>gba</name>", blk):
        return blk
    return EMULATOR.sub(emulator, blk)
r = BLOCK.sub(system, s)
if not added:
    sys.exit(1)
ET.fromstring(r.encode("utf-8"))
open(p, "w", encoding="utf-8").write(r)
print("  %s: armGBA offered for GBA in %s" % (p, ", ".join(added)))
PY
echo "$F" >> /var/lib/darkos-ota/merged
exit 0
