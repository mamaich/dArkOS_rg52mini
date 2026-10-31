#!/usr/bin/env python3
"""RetroArch from master as a second choice beside the release, in EMULATOR SETTINGS.

/usr/local/bin/retroarch-next runs RetroArch built from libretro's master
(/opt/retroarch-next, 64-bit only) with its own retroarch.cfg in
~/.config/retroarch-next; the cores, BIOS, saves and the rest are the release's.
The release stays every system's default.

For each system started by RetroArch's own command line,
  /usr/local/bin/%EMULATOR% -L /home/ark/.config/%EMULATOR%/cores/%CORE%_libretro.so
this adds an emulator "retroarch-next" with the same cores, after "retroarch".
Systems whose command is a script of their own (n64.sh, dreamcast.sh ...) are
left alone: those scripts tell the emulators apart by name.

Usage: es-retroarch-next.py es_systems.cfg   (edits the file in place; running
it again changes nothing). Run by build_emulationstation-rk3562.sh on the RG52 Mini.
"""
import re
import sys
import xml.etree.ElementTree as ET

BLOCK = re.compile(r"[ \t]*<system>.*?</system>[ \t]*\n", re.S)
DIRECT = "/usr/local/bin/%EMULATOR% -L /home/ark/.config/%EMULATOR%/cores/%CORE%_libretro.so"
EMULATOR = re.compile(r'([ \t]*)<emulator name="retroarch">.*?</emulator>[ \t]*\n', re.S)


def main(cfg):
    text = open(cfg, encoding="utf-8").read()
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

    result = BLOCK.sub(system, text)
    ET.fromstring(result.encode("utf-8"))   # still well-formed, or stop here
    if result != text:
        open(cfg, "w", encoding="utf-8").write(result)
    print("es-retroarch-next: retroarch-next offered for %d systems in %s" % (len(added), cfg))
    if added:
        print("  " + " ".join(added))


if __name__ == "__main__":
    main(sys.argv[1])
