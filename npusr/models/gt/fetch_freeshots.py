#!/usr/bin/env python3
"""Скриншоты СВОБОДНЫХ игр с Wikimedia Commons для обучения SR (идея владельца 24.11): только файлы со свободной
лицензией (CC0/PD/CC BY/CC BY-SA/GPL/…), размер >= 1000x600. Пишет dl/freeshots/img/* и manifest.json (заголовок, страница,
лицензия, автор, игра, sha256) — для NOTICE. Повторный запуск докачивает недостающее.
usage: fetch_freeshots.py [макс_на_игру=12]"""
import json, os, re, sys, time, hashlib, html, urllib.parse, urllib.request, urllib.error
D = "/home/mamaich/rg52/aisr/dl/freeshots"; os.makedirs(f"{D}/img", exist_ok=True)
MAXG = int(sys.argv[1]) if len(sys.argv) > 1 else 12
API = "https://commons.wikimedia.org/w/api.php"
UA = {"User-Agent": "SRDatasetFetch/0.2 (free-licensed game screenshots, with a licence manifest; one file per 10 s)"}
GAMES = ["SuperTuxKart", "SuperTux", "0 A.D.", "Xonotic", "Battle for Wesnoth", "OpenTTD", "Freeciv", "Minetest", "Luanti", "Hedgewars",
         "Warzone 2100", "FlightGear", "Red Eclipse", "Teeworlds", "Widelands", "Pingus", "Extreme Tux Racer", "Nexuiz", "OpenArena",
         "Sauerbraten", "Tremulous", "Warsow", "Veloren", "Mindustry", "Endless Sky", "Freedoom", "Speed Dreams", "TORCS", "VDrift",
         "Stunt Rally", "Naev", "UFO Alien Invasion", "MegaGlest", "Secret Maryo Chronicles", "Oolite", "Unknown Horizons", "FreeCol",
         "Zero-K", "Unvanquished", "Supertuxkart 1.0", "Frogatto", "Tux Racer", "Neverball", "Armagetron", "Simutrans", "Cataclysm DDA",
         "Pioneer space simulator", "Thrive game", "OpenMW", "Rigs of Rods", "Tales of Maj'Eyal", "Lugaru", "Flare RPG", "Pixel Dungeon",
         "Mari0", "Xmoto", "Globulation 2", "Scorched 3D", "Yo Frankie", "Sintel The Game", "Ryzom", "Terasology", "AssaultCube"]
OK = re.compile(r"^(CC0|Public domain|PD.*|CC[ -]BY(-SA)?[ -][0-9.]+.*|GPL.*|GNU GPL.*|LGPL.*|AGPL.*|GFDL.*|MIT|Expat|BSD.*|Zlib.*|Apache.*|Artistic.*|FAL|Copyrighted free use|WTFPL)$", re.I)


def get(params):
    q = API + "?" + urllib.parse.urlencode({**params, "format": "json"})
    for k in range(4):
        try:
            with urllib.request.urlopen(urllib.request.Request(q, headers=UA), timeout=40) as r: return json.load(r)
        except Exception as e:
            print("  повтор:", e, flush=True); time.sleep(5 * (k + 1))
    return {}


strip = lambda s: re.sub(r"\s+", " ", html.unescape(re.sub(r"<[^>]+>", "", s or ""))).strip()[:200]
mf = f"{D}/manifest.json"; man = json.load(open(mf)) if os.path.exists(mf) else {}
for g in GAMES:
    have = sum(1 for v in man.values() if v["game"] == g)
    if have >= MAXG: continue
    r = get({"action": "query", "generator": "search", "gsrsearch": f'"{g}" screenshot', "gsrnamespace": 6, "gsrlimit": 50,
             "prop": "imageinfo", "iiprop": "url|size|mime|extmetadata"})
    cand = []
    for p in (r.get("query", {}).get("pages", {}) or {}).values():
        ii = (p.get("imageinfo") or [{}])[0]; em = ii.get("extmetadata", {})
        lic = strip(em.get("LicenseShortName", {}).get("value", "")); t = p.get("title", "")
        if ii.get("mime") not in ("image/png", "image/jpeg") or ii.get("width", 0) < 1000 or ii.get("height", 0) < 600: continue
        if not OK.match(lic) or em.get("NonFree"): continue
        if ii["width"] * ii["height"] > 9e6 or t in man: continue
        key = re.sub(r"[^a-z0-9]", "", g.lower())
        if key[:6] not in re.sub(r"[^a-z0-9]", "", t.lower()): continue      # название игры — в имени файла
        cand.append((ii["mime"] != "image/png", -ii["width"], t, ii, lic, strip(em.get("Artist", {}).get("value", ""))))
    cand.sort(); n = 0
    for _, _, t, ii, lic, art in cand[:MAXG - have]:
        fn = re.sub(r"[^A-Za-z0-9._-]", "_", t.split(":", 1)[1])[:120]
        b = None
        for att in range(4):   # Commons ограничивает частоту (429): ждать, сколько просят, и не чаще файла в 6 с
            try:
                with urllib.request.urlopen(urllib.request.Request(ii["url"], headers=UA), timeout=90) as rr: b = rr.read()
                break
            except urllib.error.HTTPError as e:
                if e.code != 429: print("  не скачан:", t, e.code, flush=True); break
                w = int(e.headers.get("Retry-After") or 0); w = min(max(w, 60 * (att + 1)), 600)
                print(f"  429, жду {w} с", flush=True); time.sleep(w)
            except Exception as e:
                print("  не скачан:", t, str(e)[:80], flush=True); break
        if b is None: time.sleep(6); continue
        open(f"{D}/img/{fn}", "wb").write(b)
        man[t] = {"file": fn, "game": g, "license": lic, "artist": art, "page": ii.get("descriptionurl"), "url": ii["url"],
                  "w": ii["width"], "h": ii["height"], "mime": ii["mime"], "sha256": hashlib.sha256(b).hexdigest()}
        n += 1; time.sleep(10.0)
    json.dump(man, open(mf, "w"), ensure_ascii=False, indent=1)
    print(f"{g}: кандидатов {len(cand)}, скачано {n}, всего {len(man)}", flush=True); time.sleep(1.0)
lic = {}
for v in man.values(): lic[v["license"]] = lic.get(v["license"], 0) + 1
print("итого файлов", len(man), "лицензии:", lic)
