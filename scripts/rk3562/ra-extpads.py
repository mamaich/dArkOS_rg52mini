#!/usr/bin/env python3
"""dArkOS RG52 Mini: USB pads first in RetroArch, the console's own controls last.

ArkOS on the RK3566 consoles makes an external pad player 1: retroarch.cfg.ext swaps
the first two pads, and controller_setup.sh writes a RetroArch profile for a pad from
EmulationStation's mapping of it. On the RG52 Mini the order of the pads RetroArch sees
is not fixed (a pad plugged in at boot can come before the console's controls, one
plugged in later after them), so this works it out at each start:

  ra-extpads.py <RetroArch config dir>

prints the path of a config for --appendconfig, or nothing when no USB pad with a
mapping is in (RetroArch starts as before). The config:
- input_playerN_joypad_index: the USB pads RetroArch can map, in RetroArch's own order
  (devnodes compared as strings, as udev_joypad.c sorts them), then the console's
  controls, then any other pad;
- player 1's binds empty, so every pad plays by its profile (players 2 and on have
  them empty already);
- the hotkeys in player 1's pad's numbering (RetroArch takes hotkeys from player 1's
  pad only): Home is the pad's guide button (BTN_MODE) when it has one, otherwise
  EmulationStation's hotkey button; then Start exits, X opens the menu, R1 saves,
  L1 loads, B resets, R2 held fast-forwards, up and down set the brightness, as
  Home and those on the console.
A pad with no profile gets one written from EmulationStation's mapping (es_input.cfg,
picked as ES picks it: the GUID, then the GUID without SDL's name CRC, then the name).
A profile this did not write is left alone. config_save_on_exit is off in dArkOS, so
nothing of this is written back to retroarch.cfg.
"""
import os
import sys
import xml.etree.ElementTree as ET

BUILTIN = "rk3562-joystick"
ES_INPUT = "/etc/emulationstation/es_input.cfg"
MARK = "# dArkOS: made from EmulationStation's mapping by ra-extpads.py"
KEY_UP, KEY_DOWN, BTN_MISC, KEY_MAX, BTN_MODE = 103, 108, 0x100, 0x2FF, 0x13C
MAX_USERS = 8


def read(path):
    try:
        with open(path) as f:
            return f.read().strip()
    except OSError:
        return ""


def bitmap(words):
    """A sysfs capability bitmap: hex longs, the most significant first."""
    n = 0
    for w in words.split():
        n = (n << 64) | int(w, 16)
    return n


def pads():
    """The pads RetroArch's udev driver opens, in its order."""
    out = []
    for ev in os.listdir("/sys/class/input"):
        if not ev.startswith("event"):
            continue
        sysdir = "/sys/class/input/" + ev
        dev = read(sysdir + "/dev")
        if ":" not in dev:
            continue
        if "E:ID_INPUT_JOYSTICK=1" not in read("/run/udev/data/c" + dev).splitlines():
            continue
        d = sysdir + "/device"
        num = lambda n: int(read(d + "/id/" + n) or "0", 16)
        out.append({"node": "/dev/input/" + ev, "name": read(d + "/name"),
                    "bus": num("bustype"), "vendor": num("vendor"),
                    "product": num("product"), "version": num("version"),
                    "keys": bitmap(read(d + "/capabilities/key"))})
    out.sort(key=lambda p: p["node"].encode())
    return out[:MAX_USERS]


def button_index(keys, code):
    """RetroArch's button number for a key code (udev_joypad.c, udev_add_pad)."""
    order = (list(range(KEY_UP, KEY_DOWN + 1)) + list(range(BTN_MISC, KEY_MAX))
             + list(range(0, KEY_UP)) + list(range(KEY_DOWN + 1, BTN_MISC)))
    i = 0
    for c in order:
        if keys >> c & 1:
            if c == code:
                return i
            i += 1
            if i >= 64:
                break
    return None


def crc16(data):
    crc = 0
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


def guid(p, crc=0):
    le = lambda v: "%02x%02x" % (v & 0xFF, v >> 8 & 0xFF)
    return (le(p["bus"]) + le(crc) + le(p["vendor"]) + "0000" + le(p["product"]) + "0000"
            + le(p["version"]) + "0000")


def es_mapping(root, p):
    """EmulationStation's mapping of the pad, picked as ES picks it."""
    nodes = [n for n in root.findall("inputConfig") if n.get("type") == "joystick"]
    nocrc = lambda g: g[:4] + "0000" + g[8:] if len(g) == 32 else g
    if p["vendor"] or p["product"]:
        full = guid(p, crc16(p["name"].encode()))
        for n in nodes:
            if n.get("deviceGUID", "").lower() == full:
                return n
        for n in nodes:
            if nocrc(n.get("deviceGUID", "").lower()) == guid(p):
                return n
    for n in nodes:
        if n.get("deviceName") == p["name"]:
            return n
    return None


def es_inputs(node):
    m = {}
    for i in node.findall("input"):
        m[i.get("name")] = (i.get("type"), int(i.get("id", "0")), int(i.get("value", "1")))
    return m


HAT = {1: "up", 2: "right", 4: "down", 8: "left"}


def bind(inp):
    """A RetroArch bind (key suffix, value) for one EmulationStation input."""
    kind, ident, value = inp
    if kind == "button":
        return "btn", str(ident)
    if kind == "hat":
        return "btn", "h%d%s" % (ident, HAT.get(value, "up"))
    if kind == "axis":
        return "axis", ("-" if value < 0 else "+") + str(ident)
    return None


def first(m, *names):
    for n in names:
        if n in m:
            return m[n]
    return None


def profile_lines(p, m):
    """The RetroPad's binds for a profile, from EmulationStation's names (fcamod's and
    Batocera's)."""
    lines = []

    def put(ra, *names):
        inp = first(m, *names)
        b = inp and bind(inp)
        if b:
            lines.append('input_%s_%s = "%s"' % (ra, b[0], b[1]))

    for ra, names in (("a", ("a",)), ("b", ("b",)), ("x", ("x",)), ("y", ("y",)),
                      ("l", ("pageup", "leftshoulder")), ("r", ("pagedown", "rightshoulder")),
                      ("l2", ("l2", "lefttrigger")), ("r2", ("r2", "righttrigger")),
                      ("l3", ("l3", "leftthumb")), ("r3", ("r3", "rightthumb")),
                      ("select", ("select",)), ("start", ("start",)),
                      ("up", ("up",)), ("down", ("down",)),
                      ("left", ("left",)), ("right", ("right",))):
        put(ra, *names)

    # sticks: fcamod keeps one direction an axis (joystick1left, value -1),
    # Batocera each of the four
    for stick, horiz, vert in (("l", "joystick1left", "joystick1up"),
                               ("r", "joystick2left", "joystick2up")):
        for axis, name in (("x", horiz), ("y", vert)):
            inp = m.get(name)
            if inp and inp[0] == "axis":
                neg = ("-" if inp[2] < 0 else "+") + str(inp[1])
                pos = ("+" if inp[2] < 0 else "-") + str(inp[1])
                lines.append('input_%s_%s_minus_axis = "%s"' % (stick, axis, neg))
                lines.append('input_%s_%s_plus_axis = "%s"' % (stick, axis, pos))
        side = "left" if stick == "l" else "right"
        for axis, minus, plus in (("x", "left", "right"), ("y", "up", "down")):
            for d, name in (("minus", minus), ("plus", plus)):
                inp = m.get("%sanalog%s" % (side, name))
                key = "input_%s_%s_%s_axis" % (stick, axis, d)
                if inp and inp[0] == "axis" and not any(l.startswith(key + " ") for l in lines):
                    lines.append('%s = "%s%d"' % (key, "-" if inp[2] < 0 else "+", inp[1]))
    return lines


def hotkey_lines(p, m):
    """The hotkeys for player 1's pad, in its numbering; every other hotkey bind empty."""
    out = {}

    def put(hotkey, inp):
        b = inp and bind(inp)
        if b:
            out["input_%s_%s" % (hotkey, b[0])] = b[1]

    mode = button_index(p["keys"], BTN_MODE)
    put("enable_hotkey", ("button", mode, 1) if mode is not None else first(m, "hotkey", "hotkeyenable"))
    put("exit_emulator", first(m, "start"))
    put("menu_toggle", first(m, "x"))
    put("save_state", first(m, "pagedown", "rightshoulder"))
    put("load_state", first(m, "pageup", "leftshoulder"))
    put("reset", first(m, "b"))
    put("hold_fast_forward", first(m, "r2", "righttrigger"))
    put("brightness_up", first(m, "up"))
    put("brightness_down", first(m, "down"))
    return out


def ra_setting(cfg, key):
    for line in read(cfg).splitlines():
        k, _, v = line.partition("=")
        if k.strip() == key:
            return v.strip().strip('"')
    return ""


def main():
    confdir = sys.argv[1]
    cfg = os.path.join(confdir, "retroarch.cfg")
    found = pads()
    externals = [p for p in found if p["name"] != BUILTIN]
    if not externals or not os.path.exists(ES_INPUT):
        return
    autodir = ra_setting(cfg, "joypad_autoconfig_dir").replace("~", "/home/ark", 1)
    udevdir = os.path.join(autodir or os.path.join(confdir, "autoconfig"), "udev")
    root = ET.parse(ES_INPUT).getroot()

    mapped = []
    for p in externals:
        node = es_mapping(root, p)
        if node is None:
            continue
        m = es_inputs(node)
        p["es"] = m
        # a profile of its own (official, the user's) is left alone; ours follows ES
        ours = os.path.join(udevdir, p["name"].replace("/", "_") + ".cfg")
        theirs = False
        for f in os.listdir(udevdir) if os.path.isdir(udevdir) else []:
            text = read(os.path.join(udevdir, f))
            if ('input_device = "%s"' % p["name"]) in text and MARK not in text:
                theirs = True
        if not theirs:
            body = "\n".join([MARK, 'input_driver = "udev"', 'input_device = "%s"' % p["name"],
                              'input_vendor_id = "%d"' % p["vendor"],
                              'input_product_id = "%d"' % p["product"]] + profile_lines(p, m)) + "\n"
            if read(ours) + "\n" != body:
                os.makedirs(udevdir, exist_ok=True)
                with open(ours, "w") as f:
                    f.write(body)
        mapped.append(p)
    if not mapped:
        return

    builtin = [p for p in found if p["name"] == BUILTIN]
    order = mapped + builtin + [p for p in found if p not in mapped and p not in builtin]
    index = {p["node"]: i for i, p in enumerate(found)}
    lines = ["# dArkOS: made by ra-extpads.py for this start"]
    used = [index[p["node"]] for p in order]
    free = [i for i in range(MAX_USERS) if i not in used]
    for user in range(MAX_USERS):
        idx = used[user] if user < len(used) else free[user - len(used)]
        lines.append('input_player%d_joypad_index = "%d"' % (user + 1, idx))

    # player 1's binds empty: its pad plays by its profile
    for line in read(cfg).splitlines():
        k = line.partition("=")[0].strip()
        if k.startswith("input_player1_") and k.endswith(("_btn", "_axis")):
            lines.append('%s = "nul"' % k)

    # every hotkey bind empty, then player 1's pad's
    hot = hotkey_lines(mapped[0], mapped[0]["es"])
    for line in read(cfg).splitlines():
        k = line.partition("=")[0].strip()
        if (k.startswith("input_") and not k.startswith("input_player")
                and k.endswith(("_btn", "_axis")) and k not in hot):
            lines.append('%s = "nul"' % k)
    for k, v in hot.items():
        lines.append('%s = "%s"' % (k, v))

    out = "/tmp/ra-extpads-%s.cfg" % os.path.basename(os.path.normpath(confdir))
    with open(out, "w") as f:
        f.write("\n".join(lines) + "\n")
    print(out)


if __name__ == "__main__":
    try:
        main()
    except Exception as e:  # never stop a game from starting
        print("ra-extpads: %s" % e, file=sys.stderr)
