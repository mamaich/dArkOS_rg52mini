#!/usr/bin/env python3
"""Load RetroArch slang presets one by one on the console and say which work.

    sudo slangcheck.py --root DIR --list LIST --out RESULTS.tsv [options]

On the RG52 Mini, as root, with ES's menu in front (it stops ES, and starts
it again at the end). It runs the 64-bit RetroArch on its Vulkan driver with
a light core and a test ROM (Genesis Plus GX, the 240p Test Suite), sound
off and the volume at 0, from a copy of ark's retroarch.cfg with
--appendconfig (nothing of the user's is written), and for each preset of
LIST (paths relative to DIR, or absolute) sends SET_SHADER over RetroArch's
network commands, then GET_STATUS, whose answer comes once the preset has
been loaded or refused; then it lets RetroArch draw for a moment with it.
Each preset comes out as

  ok     loaded and drawn
  fail   RetroArch refused it (the stock shader stays; the first error lines)
  crash  RetroArch died loading or drawing it (it is started again)
  hang   no answer within --timeout (RetroArch is killed and started again)

with the seconds it took, in RESULTS.tsv (status, seconds, preset, detail).
Run it on the old set and the new one with the same RetroArch (slangdeps.py
lists the presets a new set changes) and compare: a preset that worked with
the old set and does not with the new one is a reason to pin the shaders
back (build_retroarch.sh, SLANG_COMMIT). The owner, 2026-11-28: take
upstream's newest shaders, checked so (1.13's crash of RetroArch 1.22.2 on
presets sized from the source: docs/KNOWN-ISSUES.md, "RetroArch's shaders on
the Mali-G52").
"""
import argparse
import os
import re
import signal
import socket
import subprocess
import sys
import time

RA = "/opt/retroarch/bin/retroarch"
CFG = "/home/ark/.config/retroarch/retroarch.cfg"
CORE = "/home/ark/.config/retroarch/cores/genesis_plus_gx_libretro.so"
ROM = "/roms/megadrive/240p Test Suite.bin"
WORK = "/tmp/slangcheck"
PORT = 55355
EMUS = re.compile(r"^(retroarch|retroarch32|PPSSPPSDL|dolphin-emu|armsx2|drastic|dsperate|pugwash|"
                  r"dialog|kodi|re3|gptokeyb|azahar|mupen64plus|flycast)")
FAILED = re.compile(r"Failed applying shader|Failed to create preset|Failed to (load|parse|compile|create)", re.I)
DETAIL = re.compile(r"(\[(slang|Vulkan|Shaders?|SPIR-?V|glslang)[^\]]*\].*(fail|error|invalid|refus|unknown|not |missing))"
                    r"|^\s*ERROR:|Failed applying shader", re.I)
# The owner's cfg keeps saves, states and screenshots beside the game (*_in_content_dir), which
# beats the folders below unless turned off here too.
OVERRIDES = """video_driver = "vulkan"
video_shader_enable = "true"
video_shader_delay = "0"
auto_shaders_enable = "false"
network_cmd_enable = "true"
network_cmd_port = "%d"
config_save_on_exit = "false"
audio_enable = "false"
savestate_auto_load = "false"
savestate_auto_save = "false"
savefiles_in_content_dir = "false"
savestates_in_content_dir = "false"
screenshots_in_content_dir = "false"
savefile_directory = "%s/saves"
savestate_directory = "%s/states"
screenshot_directory = "%s"
cheevos_enable = "false"
rewind_enable = "false"
fps_show = "false"
log_verbosity = "true"
frontend_log_level = "1"
""" % (PORT, WORK, WORK, WORK)


def run(*cmd):
    return subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True).stdout


def foreground_free():
    emus = [c for c in run("ps", "-eo", "comm=").split() if EMUS.match(c)]
    es = run("systemctl", "is-active", "emulationstation").strip()
    return es == "active" and not emus, "ES %s, emulators: %s" % (es, " ".join(emus) or "none")


def volume():
    m = re.search(r"Playback (\d+) \[", run("amixer", "-c", "0", "sget", "Master"))
    return m.group(1) if m else None


class RetroArch:
    def __init__(self, a):
        self.a, self.p = a, None
        self.log = os.path.join(WORK, "retroarch.log")
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("127.0.0.1", 0))

    def size(self):
        try:
            return os.path.getsize(self.log)
        except OSError:
            return 0

    def since(self, off):
        with open(self.log, "rb") as f:
            f.seek(off)
            return f.read().decode("utf-8", "replace").splitlines()

    def alive(self):
        return self.p is not None and self.p.poll() is None

    def send(self, msg):
        self.sock.sendto(msg.encode(), ("127.0.0.1", PORT))

    def drain(self):
        self.sock.setblocking(False)
        try:
            while True:
                self.sock.recv(4096)
        except OSError:
            pass
        self.sock.setblocking(True)

    def status(self, timeout):
        """GET_STATUS's answer, or None: within timeout, or as soon as RetroArch is gone."""
        self.drain()
        self.send("GET_STATUS")
        end = time.time() + timeout
        while time.time() < end:
            self.sock.settimeout(min(1.0, max(0.05, end - time.time())))
            try:
                return self.sock.recv(4096).decode("utf-8", "replace").strip()
            except socket.timeout:
                if not self.alive():
                    return None
        return None

    def start(self):
        env = ["HOME=/home/ark", "USER=ark", "SDL_VIDEO_EGL_DRIVER=libEGL.so"]
        if self.a.vulkan:
            env.append("DARKOS_VULKAN=" + self.a.vulkan)
        out = open(self.log, "ab")
        out.write(b"\n=== slangcheck: RetroArch started\n")
        out.flush()
        self.p = subprocess.Popen(["runuser", "-u", "ark", "--", "env"] + env +
                                  [RA, "-c", os.path.join(WORK, "retroarch.cfg"),
                                   "--appendconfig=" + os.path.join(WORK, "overrides.cfg"),
                                   "--verbose", "-L", self.a.core, self.a.rom],
                                  stdout=out, stderr=subprocess.STDOUT, start_new_session=True,
                                  cwd="/home/ark")
        out.close()
        end = time.time() + 90
        while time.time() < end and self.alive():
            r = self.status(2)
            if r and "PLAYING" in r:
                time.sleep(2)
                return True
            time.sleep(0.5)
        return False

    def stop(self):
        if self.p is None:
            return
        if self.alive():
            os.killpg(self.p.pid, signal.SIGTERM)
            try:
                self.p.wait(8)
            except subprocess.TimeoutExpired:
                os.killpg(self.p.pid, signal.SIGKILL)
                self.p.wait(5)
        self.p = None


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--root", required=True, help="the presets' folder (a shaders_slang)")
    ap.add_argument("--list", required=True, help="presets to load, one a line, relative to --root")
    ap.add_argument("--out", required=True, help="results, tab separated")
    ap.add_argument("--core", default=CORE)
    ap.add_argument("--rom", default=ROM)
    ap.add_argument("--vulkan", choices=["libmali", "malisx2"], help="the Vulkan driver (default: vulkan.conf's)")
    ap.add_argument("--timeout", type=float, default=300, help="seconds a preset may take to load")
    ap.add_argument("--settle", type=float, default=2.0, help="seconds RetroArch draws with each")
    ap.add_argument("--restart-every", type=int, default=60, help="a fresh RetroArch after this many")
    a = ap.parse_args()
    # sudo over ssh keeps a PATH without sbin (runuser)
    os.environ["PATH"] = "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"
    if os.geteuid() != 0:
        sys.exit("run as root (it stops and starts ES)")
    free, why = foreground_free()
    if not free:
        sys.exit("not ES's menu in front (%s): nothing done" % why)
    presets = [l.strip() for l in open(a.list) if l.strip() and not l.startswith("#")]
    os.makedirs(os.path.join(WORK, "saves"), exist_ok=True)
    os.makedirs(os.path.join(WORK, "states"), exist_ok=True)
    with open(CFG) as f, open(os.path.join(WORK, "retroarch.cfg"), "w") as g:
        g.write(f.read())
    with open(os.path.join(WORK, "overrides.cfg"), "w") as g:
        g.write(OVERRIDES)
    subprocess.run(["chown", "-R", "ark:ark", WORK])
    vol = volume()
    ra = RetroArch(a)
    open(ra.log, "wb").close()          # this run's log only
    subprocess.run(["chown", "ark:ark", ra.log])

    def cleanup(*_):
        ra.stop()
        if vol is not None:
            subprocess.run(["amixer", "-c", "0", "-q", "sset", "Master", vol])
        subprocess.run(["systemctl", "start", "emulationstation"])
        print("cleaned up: RetroArch stopped, volume %s, ES %s"
              % (vol, run("systemctl", "is-active", "emulationstation").strip()), flush=True)

    for s in (signal.SIGTERM, signal.SIGHUP, signal.SIGINT):
        signal.signal(s, lambda *_: (cleanup(), os._exit(1)))
    res = open(a.out, "w")
    counts = {}
    try:
        subprocess.run(["amixer", "-c", "0", "-q", "sset", "Master", "0"])
        subprocess.run(["systemctl", "stop", "emulationstation"])
        time.sleep(2)
        since_start = 0
        for i, rel in enumerate(presets, 1):
            path = rel if os.path.isabs(rel) else os.path.join(a.root, rel)
            if not ra.alive() or since_start >= a.restart_every:
                ra.stop()
                if not ra.start():
                    tail = " | ".join(ra.since(max(0, ra.size() - 1500))[-4:])
                    sys.exit("RetroArch did not start with the stock shader: " + tail)
                since_start = 0
            since_start += 1
            off, t0 = ra.size(), time.time()
            ra.drain()
            ra.send("SET_SHADER " + path)
            r = ra.status(a.timeout)
            dt = time.time() - t0
            if r is not None:
                time.sleep(a.settle)
            log = ra.since(off)
            if not ra.alive():
                st = "crash"
            elif r is None:
                st = "hang"
                ra.stop()
            elif any(FAILED.search(l) for l in log):
                st = "fail"
            else:
                st = "ok"
            det = [l.strip() for l in log if DETAIL.search(l)]
            if st in ("crash", "hang") and not det:
                det = [l.strip() for l in log[-3:]]
            detail = " | ".join(det[:3])[:400]
            counts[st] = counts.get(st, 0) + 1
            res.write("%s\t%.1f\t%s\t%s\n" % (st, dt, rel, detail))
            res.flush()
            print("[%d/%d] %-5s %6.1f s  %s%s" % (i, len(presets), st, dt, rel,
                                                 ("  " + detail[:160]) if st != "ok" else ""), flush=True)
    finally:
        res.close()
        cleanup()
    print("done: " + ", ".join("%s %d" % kv for kv in sorted(counts.items())), flush=True)


if __name__ == "__main__":
    main()
