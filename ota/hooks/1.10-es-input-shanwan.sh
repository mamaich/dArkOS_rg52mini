#!/bin/bash
# 1.10: ES knows the ShanWan "PS3/PC Wired GamePad" (USB 2563:0523) without CONFIGURE INPUT. The
# update puts its config only into the image's es_input.cfg; ES writes that file whenever a pad is
# configured, so on most devices it is the user's own and the update keeps it (the image's beside it
# as es_input.cfg.ota-new). The config is added to it here unless it has one for this pad already,
# by its GUID (with SDL's name CRC or without) or its name: a pad the user configured stays as the
# user set it. An untouched file is the image's already. Safe to run twice.
#
# Never fails the update. A changed file goes to merged for a rollback.
PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
F=${ES_INPUT:-/etc/emulationstation/es_input.cfg}
[ -f "$F" ] || exit 0
python3 - "$F" <<'PY' || exit 0
import sys
import xml.etree.ElementTree as ET
p = sys.argv[1]
s = open(p, encoding="utf-8").read()
NAME = "ShanWan PS3/PC Wired GamePad"
GUID = "03000000632500002305000010010000"
root = ET.fromstring(s.encode("utf-8"))
for n in root.iter("inputConfig"):
    g = n.get("deviceGUID", "").lower()
    if n.get("deviceName") == NAME or (len(g) == 32 and g[:4] + "0000" + g[8:] == GUID):
        sys.exit(1)
END = "</inputList>"
if s.count(END) != 1:
    sys.exit(1)
btn = lambda name, i: '\t\t<input name="%s" type="button" id="%d" value="1" code="%d" />' % (name, i, 288 + i)
hat = lambda name, v: '\t\t<input name="%s" type="hat" id="0" value="%d" code="16" />' % (name, v)
axis = lambda name, i, v, c: '\t\t<input name="%s" type="axis" id="%d" value="%d" code="%d" />' % (name, i, v, c)
rows = [btn("a", 1), btn("b", 2), hat("down", 4), btn("hotkey", 8), hat("left", 8),
        axis("leftanalogdown", 1, 1, 1), axis("leftanalogleft", 0, -1, 0),
        axis("leftanalogright", 0, 1, 0), axis("leftanalogup", 1, -1, 1),
        btn("leftthumb", 10), btn("lefttrigger", 6), btn("pagedown", 5), btn("pageup", 4),
        hat("right", 2),
        axis("rightanalogdown", 3, 1, 5), axis("rightanalogleft", 2, -1, 2),
        axis("rightanalogright", 2, 1, 2), axis("rightanalogup", 3, -1, 5),
        btn("rightthumb", 11), btn("righttrigger", 7), btn("select", 8), btn("start", 9),
        hat("up", 1), btn("x", 0), btn("y", 3)]
entry = ('\t<inputConfig type="joystick" deviceName="%s" deviceGUID="%s">\n' % (NAME, GUID)
         + "\n".join(rows) + "\n\t</inputConfig>\n")
r = s.replace(END, entry + END)
ET.fromstring(r.encode("utf-8"))
open(p, "w", encoding="utf-8").write(r)
print("  %s: the ShanWan PS3/PC Wired GamePad added" % p)
PY
echo "$F" >> /var/lib/darkos-ota/merged
exit 0
