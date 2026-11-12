#!/bin/bash
# 1.9: EMULATOR SETTINGS offers armN64 for the Nintendo 64, last in the "retroarch" emulator's list
# (parallel_n64 stays the default). The update puts it only into the image's es_systems.cfg; a
# device whose es_systems.cfg is the user's own copy (kept by the update, the image's beside it as
# es_systems.cfg.ota-new) gets the same core in it here; the rest of the file stays. An untouched
# file is the image's already. Safe to run twice.
#
# And the core's resolution: the image's core options start it at the console's own (Resolution
# Native; at Auto, twice that here, Zelda's title runs at 46 frames a second for 60: the frame the
# core draws in its own GLES context is copied to RetroArch's Vulkan one). RetroArch's options
# file is the user's on any device that has run a game, so the update keeps it; the line is added
# to it here unless the user has chosen a resolution for the core already.
#
# Never fails the update. Changed files go to merged for a rollback.
PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
F=/etc/emulationstation/es_systems.cfg
[ -f /home/ark/.config/retroarch/cores/armn64_libretro.so ] || exit 0
O=/home/ark/.config/retroarch/retroarch-core-options.cfg
if [ -f "$O" ] && ! grep -q '^armn64_resolution ' "$O"; then
  echo 'armn64_resolution = "1"' >> "$O" && echo "$O" >> /var/lib/darkos-ota/merged &&
    echo "  $O: armN64 at the console's own resolution"
fi
[ -f "$F" ] || exit 0
python3 - "$F" <<'PY' || exit 0
import re, sys
import xml.etree.ElementTree as ET
p = sys.argv[1]
s = open(p, encoding="utf-8").read()
BLOCK = re.compile(r"[ \t]*<system>.*?</system>[ \t]*\n", re.S)
EMULATOR = re.compile(r'[ \t]*<emulator name="(retroarch|retroarch-next)">.*?</emulator>[ \t]*\n', re.S)
# after the last N64 core the list has
CORES = re.compile(r"([ \t]*)<core>(?:mupen64plus|mupen64plus_next|parallel_n64)</core>[ \t]*\n")
added = []
def emulator(m):
    e = m.group(0)
    if "<core>armn64</core>" in e:
        return e
    last = None
    for last in CORES.finditer(e):
        pass
    if last is None:
        return e
    added.append(m.group(1))
    return e[:last.end()] + last.group(1) + "<core>armn64</core>\n" + e[last.end():]
def system(m):
    blk = m.group(0)
    if not re.search(r"<name>n64</name>", blk):
        return blk
    return EMULATOR.sub(emulator, blk)
r = BLOCK.sub(system, s)
if not added:
    sys.exit(1)
ET.fromstring(r.encode("utf-8"))
open(p, "w", encoding="utf-8").write(r)
print("  %s: armN64 offered for the Nintendo 64 in %s" % (p, ", ".join(added)))
PY
echo "$F" >> /var/lib/darkos-ota/merged
exit 0
