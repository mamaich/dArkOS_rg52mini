#!/bin/bash
# 1.8 brings Zeebo: the zeebx core (ZeebxTeam/zeebx-emu) under RetroArch, ES system "zeebo"
# reading /roms/zeebo. A device whose es_systems.cfg is the user's own copy (kept by the update,
# the image's beside it as es_systems.cfg.ota-new) gets the same system in it here, before
# "videos" (or at the end of the list); the rest of the file stays. An untouched file is the
# image's already. And the ROM folder a new image has. Safe to run twice.
#
# Never fails the update. Changed files go to merged for a rollback; the new folder is not
# listed (a rollback leaves it, empty or not).
PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
if [ -d /roms/nds ] && [ ! -e /roms/zeebo ]; then
  mkdir /roms/zeebo && chown --reference=/roms/nds /roms/zeebo 2>/dev/null
  echo "  /roms/zeebo added"
fi
F=/etc/emulationstation/es_systems.cfg
[ -f "$F" ] || exit 0
[ -f /home/ark/.config/retroarch/cores/zeebx_libretro.so ] || exit 0
grep -q "<name>zeebo</name>" "$F" && exit 0
NEXT=0; [ -x /opt/retroarch-next/bin/retroarch ] && NEXT=1
python3 - "$F" "$NEXT" <<'PY' || exit 0
import sys
import xml.etree.ElementTree as ET
p, nxt = sys.argv[1], sys.argv[2] == "1"
s = open(p, encoding="utf-8").read()
emu = """			<emulator name="%s">
				<cores>
					<core>zeebx</core>
				</cores>
			</emulator>
"""
blk = ("""	<system>
		<name>zeebo</name>
		<fullname>Zeebo</fullname>
		<path>/roms/zeebo/</path>
		<extension>.mod .MOD .zip .ZIP .7z .7Z</extension>
		<command>sudo perfmax %GOVERNOR% %ROM%; nice -n -19 /usr/local/bin/%EMULATOR% -L /home/ark/.config/%EMULATOR%/cores/%CORE%_libretro.so %ROM%; sudo perfnorm</command>
		<emulators>
""" + emu % "retroarch" + (emu % "retroarch-next" if nxt else "") + """		</emulators>
		<platform>zeebo</platform>
		<theme>zeebo</theme>
		<manufacturer>Tectoy</manufacturer>
		<release>2009</release>
		<hardware>console</hardware>
	</system>
""")
a = "\t<system>\n\t\t<name>videos</name>"
if s.count(a) == 1:
    r = s.replace(a, blk + a)
elif s.count("</systemList>") == 1:
    r = s.replace("</systemList>", blk + "</systemList>")
else:
    sys.exit(1)
ET.fromstring(r.encode("utf-8"))
open(p, "w", encoding="utf-8").write(r)
print("  %s: Zeebo added" % p)
PY
echo "$F" >> /var/lib/darkos-ota/merged
exit 0
