#!/usr/bin/env python3
"""Скриншоты СВОБОДНЫХ игр из каталога Flathub (appstream: проект сам публикует скриншоты; лицензия проекта — в метаданных).
Список игр — вручную: только игры со свободным кодом И свободными данными (не эмуляторы и не движки под чужие данные:
на их скриншотах коммерческие игры). Пишет в тот же dl/freeshots/img и manifest.json, что и fetch_freeshots.py.
usage: fetch_flathub.py [макс_на_игру=6]"""
import json, os, re, sys, time, hashlib, urllib.request, urllib.error
D = "/home/mamaich/rg52/aisr/dl/freeshots"; os.makedirs(f"{D}/img", exist_ok=True)
MAXG = int(sys.argv[1]) if len(sys.argv) > 1 else 6
UA = {"User-Agent": "SRDatasetFetch/0.2 (free game screenshots with a licence manifest)"}
APPS = """net.supertuxkart.SuperTuxKart org.supertuxproject.SuperTux org.wesnoth.Wesnoth org.xonotic.Xonotic org.openttd.OpenTTD
org.freeciv.gtk322 org.freeciv.gtk4 org.freeciv.qt net.minetest.Minetest org.luanti.luanti org.hedgewars.Hedgewars net.wz2100.wz2100
org.flightgear.FlightGear net.redeclipse.RedEclipse com.teeworlds.Teeworlds org.widelands.Widelands net.sourceforge.ExtremeTuxRacer
com.play0ad.zeroad com.github.Anuken.Mindustry io.github.endless_sky.endless_sky org.speed_dreams.SpeedDreams org.naev.Naev
org.megaglest.MegaGlest org.pioneerspacesim.Pioneer org.armagetronad.ArmagetronAdvanced com.simutrans.Simutrans
org.cataclysmdda.CataclysmDDA com.shatteredpixel.shatteredpixeldungeon io.github.freedoom.Phase1 io.github.freedoom.Phase2
org.frozen_bubble.frozen-bubble org.unknown_horizons.UnknownHorizons org.freecol.FreeCol org.oolite.Oolite net.lugaru.Lugaru
com.agateau.PixelWheels org.tuxemon.Tuxemon net.blockattack.game io.github.retux_game.retux io.sourceforge.pysolfc.PySolFC
net.sourceforge.chromium-bsu org.neverball.Neverball org.astromenace.astromenace org.unvanquished.Unvanquished io.yorg.Yorg
party.supertux.supertuxparty com.github.yairm210.unciv org.kde.kpat org.kde.kmahjongg org.kde.ktuberling org.kde.granatier
org.kde.kbreakout org.kde.kapman org.kde.kgoldrunner org.kde.kdiamond org.kde.bomber org.kde.kbounce org.kde.palapeli org.kde.kollision
org.kde.ksirk org.kde.kblocks org.kde.katomic org.kde.klines org.kde.kolf org.kde.konquest org.kde.kshisen org.kde.kspaceduel
org.kde.kubrick org.kde.knights org.kde.picmi org.kde.killbots org.kde.ksnakeduel org.kde.bovo org.kde.kigo org.kde.kreversi
org.gnome.Mahjongg org.gnome.Quadrapassel org.gnome.Aisleriot org.gnome.Chess org.gnome.Nibbles org.gnome.Robots org.gnome.SwellFoop
org.gnome.Tetravex net.sourceforge.torcs org.vdrift.VDrift io.github.stuntrally.stuntrally3 org.zeroK.ZeroK info.beyondallreason.bar
org.tuxfamily.XMoto net.sourceforge.btanks org.maxr.maxr net.sourceforge.Scorched3D org.terasology.Terasology net.veloren.veloren
org.pingus.Pingus org.seul.pingus net.sourceforge.warzone2100 org.openclonk.OpenClonk org.zaz.Zaz org.opensurge2d.OpenSurge
io.github.mightyvikingenterprises net.sourceforge.liquidwar6 org.tuxracer.tuxracer com.github.bvschaik.julius org.gottcode.Hexalate
org.gottcode.Tanglet org.gottcode.Peg-E org.gottcode.Simsu org.gottcode.Connectagram net.sourceforge.pentobi.Pentobi
org.nongnu.pysol net.pioneerspacesim org.rigsofrods.RigsOfRods io.github.antimicrox org.megaglest org.supertux.SuperTux
net.hexaglott org.xmoto.xmoto net.sf.nootka io.github.lethal_guitar.RigelEngine com.github.rogueliketutorials org.endlessos
org.tuxfamily.supertuxkart net.minetest.minetest org.minetest.Minetest org.kde.kmines org.kde.kjumpingcube org.kde.knetwalk
org.kde.ksquares org.kde.ksudoku org.kde.lskat org.kde.blinken org.kde.klickety org.kde.kfourinline org.kde.kiriki org.kde.knavalbattle
org.gnome.Mines org.gnome.TwentyFortyEight org.gnome.Hitori org.gnome.LightsOff org.gnome.Taquin org.gnome.five-or-more
org.gnome.Four-in-a-row org.gnome.Klotski org.gnome.Reversi org.gnome.Sudoku org.gnome.Tali""".split()
BAD = {"com.github.bvschaik.julius", "io.github.lethal_guitar.RigelEngine", "io.github.antimicrox"}   # чужие данные / не игра
FREE = re.compile(r"(GPL|LGPL|AGPL|MIT|BSD|Apache|Zlib|CC0|CC-BY-[0-9]|CC-BY-SA|MPL|ISC|Artistic|OFL|FSFAP|Unlicense|WTFPL)", re.I)


def get(url, raw=False):
    try:
        with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=60) as r:
            return r.read() if raw else json.load(r)
    except urllib.error.HTTPError as e:
        return e.code
    except Exception as e:
        return str(e)[:60]


mf = f"{D}/manifest.json"; man = json.load(open(mf)) if os.path.exists(mf) else {}
okapps = 0
for app in APPS:
    if app in BAD: continue
    have = sum(1 for v in man.values() if v.get("app") == app)
    if have >= MAXG: okapps += 1; continue
    j = get(f"https://flathub.org/api/v2/appstream/{app}"); time.sleep(1.0)
    if not isinstance(j, dict): continue
    lic = j.get("project_license") or ""
    if not FREE.search(lic) or "proprietary" in lic.lower(): print(f"{app}: лицензия «{lic}» — пропуск"); continue
    urls = []
    for s in j.get("screenshots") or []:
        sz = [z for z in (s.get("sizes") or []) if z.get("src")]
        if not sz: continue
        z = max(sz, key=lambda z: int(z.get("width") or 0) * (2 if str(z.get("scale", "1x")).startswith("2") else 1))
        urls.append(z["src"])
    n = 0
    for u in urls:
        if have + n >= MAXG: break
        if u in man: continue
        b = get(u, raw=True); time.sleep(1.5)
        if not isinstance(b, bytes) or len(b) < 20000: continue
        ext = ".png" if b[:4] == b"\x89PNG" else (".jpg" if b[:2] == b"\xff\xd8" else (".webp" if b[8:12] == b"WEBP" else None))
        if ext is None: continue
        fn = re.sub(r"[^A-Za-z0-9._-]", "_", app) + f"-{have + n}{ext}"
        open(f"{D}/img/{fn}", "wb").write(b)
        man[u] = {"file": fn, "game": j.get("name") or app, "app": app, "license": lic, "artist": j.get("developer_name") or "",
                  "page": f"https://flathub.org/apps/{app}", "url": u, "source": "flathub appstream screenshot",
                  "mime": {".png": "image/png", ".jpg": "image/jpeg", ".webp": "image/webp"}[ext], "sha256": hashlib.sha256(b).hexdigest()}
        n += 1
    if n or have: okapps += 1
    json.dump(man, open(mf, "w"), ensure_ascii=False, indent=1)
    print(f"{app} ({j.get('name')}; {lic}): скриншотов {len(urls)}, скачано {n}, всего {len(man)}", flush=True)
print("игр со скриншотами:", okapps, "файлов:", len(man))
