#!/usr/bin/env python3
"""Пары (LR, HR) для обучения SR ПО НАСТОЯЩЕЙ КАРТИНКЕ высокого разрешения (без учителя ESPCN). Запрос владельца 24.11.
Игра на низком разрешении рисует без сглаживания: пиксель LR = ТОЧЕЧНАЯ выборка сцены в центре пикселя. Поэтому LR здесь —
прореживание «мастера» (картинки в Mf = S*K раз крупнее LR), а не усреднение; HR — усреднение мастера KxK (сглаженные края).
Чётный Mf: центр блока лежит между пикселями мастера — берём пиксель Mf/2, а окна HR сдвигаем на те же полпикселя
(среднее сдвигов 0 и +1 по каждой оси).
Источники:
  synth — свои сцены: многоугольники, эллипсы, линии, текст, обводки, триангуляции, градиенты, гладкие текстуры, клетки;
  shots — скриншоты свободных игр (dl/freeshots, manifest.json), прореженные;
  game  — кадры игр владельца как мастер (честные рендеры SM3DL 2x/3x, родные кадры PSP/3DS), прореженные;
  real  — настоящие пары SM3DL: S=3 — 1x -> честный 3x (pairs.npz: s0, s9); S=4 — 1x -> честный 4x (pairs4.npz, с телевизора:
          s0, s1, s9, кадры 40 и 150); сцена s2 отложена целиком;
  photo — классические пары (HR -> LR по площади): фото skimage.data и процедурные текстуры — для готовых фонов и FMV.
Выход: train/gt/x<S>_<имя>_{lr,hr}.npy (uint8, LR 64x64, HR 64S x 64S) и проверочные val_*.
usage: sr_data.py <S> [N=36000] [--val-games a,b] [--only synth,photo,shots,game,real]"""
import sys, os, glob, json, hashlib
import numpy as np, cv2
from multiprocessing import Pool
S = int(sys.argv[1]); N = int(sys.argv[2]) if len(sys.argv) > 2 else 36000
CS = 64
A = "/home/mamaich/rg52/aisr"; OUT = f"{A}/train/gt"; os.makedirs(OUT, exist_ok=True)
KS = 5 if S == 3 else 4                     # синтетика: Mf = 15 (нечётный, точный центр) для x3, 16 для x4
Yof = lambda im: cv2.cvtColor(im, cv2.COLOR_BGR2YCrCb)[..., 0] if im.ndim == 3 else im
cv2.setNumThreads(1)


def pair(m, k):
    """мастер m (uint8, размер кратен S*k) -> (LR точечно, HR усреднением k x k с поправкой центра)"""
    Mf = S * k; c = Mf // 2; H, W = m.shape
    lr = m[c::Mf, c::Mf]
    if Mf % 2:
        hr = m if k == 1 else cv2.resize(m, (W // k, H // k), interpolation=cv2.INTER_AREA)
    else:
        mp = cv2.copyMakeBorder(m, 0, 1, 0, 1, cv2.BORDER_REPLICATE).astype(np.float32)
        hr = sum(cv2.resize(mp[dy:dy + H, dx:dx + W], (W // k, H // k), interpolation=cv2.INTER_AREA) for dy in (0, 1) for dx in (0, 1)) / 4
    return np.ascontiguousarray(lr), np.clip(np.round(hr), 0, 255).astype(np.uint8)


# ---------------- синтетика
_G = {}


def grid(L):
    if L not in _G: _G[L] = np.mgrid[0:L, 0:L].astype(np.float32)
    return _G[L]


def texture(rng, L, Mf):
    """гладкая текстура (детали не мельче ~1,5 пикселя LR — как увеличенная текстура в игре)"""
    t = rng.random()
    if t < 0.65:
        im = np.zeros((L, L), np.float32)
        for _ in range(rng.integers(1, 4)):
            g = max(2, int(L / (rng.uniform(1.5, 24) * Mf)) + 2)
            im += rng.uniform(0.3, 1.0) * cv2.resize(rng.random((g, g)).astype(np.float32), (L, L), interpolation=cv2.INTER_CUBIC)
    else:
        yy, xx = grid(L); th = rng.uniform(0, np.pi); per = rng.uniform(2.5, 30) * Mf
        im = 0.5 + 0.5 * np.sin((xx * np.cos(th) + yy * np.sin(th)) * (2 * np.pi / per) + rng.uniform(0, 6.28))
    im -= im.min(); im /= max(float(im.max()), 1e-6)
    lo = rng.uniform(0, 200); hi = min(255, lo + rng.uniform(15, 255))
    return (lo + im * (hi - lo)).astype(np.uint8)


def gradient(rng, L):
    yy, xx = grid(L); th = rng.uniform(0, 2 * np.pi)
    g = (xx * np.cos(th) + yy * np.sin(th)); g = (g - g.min()) / (g.max() - g.min())
    a, b = rng.uniform(0, 255, 2)
    return (a + g * (b - a)).astype(np.uint8)


def shape_mask(rng, L, Mf, mask):
    """рисует одну фигуру в маску (255), возвращает контур для обводки или None"""
    t = rng.random()
    if t < 0.30:      # произвольный многоугольник (в т.ч. узкие клинья, как в 3D)
        pts = rng.integers(-L // 8, L + L // 8, (rng.integers(3, 7), 2)).astype(np.int32)
        cv2.fillPoly(mask, [pts], 255, lineType=cv2.LINE_8); return pts
    if t < 0.45:      # повёрнутый прямоугольник / ромб
        c = rng.uniform(0, L, 2); w = rng.uniform(2, 40, 2) * Mf; ang = rng.uniform(0, 180) if rng.random() < 0.7 else 0.0
        pts = cv2.boxPoints(((float(c[0]), float(c[1])), (float(w[0]), float(w[1])), float(ang))).astype(np.int32)
        cv2.fillPoly(mask, [pts], 255, lineType=cv2.LINE_8); return pts
    if t < 0.62:      # эллипс
        c = rng.uniform(0, L, 2); ax = rng.uniform(1.5, 30, 2) * Mf
        pts = cv2.ellipse2Poly((int(c[0]), int(c[1])), (int(ax[0]), int(ax[1])), int(rng.uniform(0, 180)), 0, 360, 3)
        cv2.fillPoly(mask, [pts], 255, lineType=cv2.LINE_8); return pts
    if t < 0.72:      # звезда / правильный многоугольник
        c = rng.uniform(0, L, 2); n = int(rng.integers(3, 9)); r1 = rng.uniform(3, 30) * Mf; r2 = r1 * rng.uniform(0.35, 1.0); a0 = rng.uniform(0, 6.28)
        ang = a0 + np.arange(2 * n) * np.pi / n; r = np.where(np.arange(2 * n) % 2 == 0, r1, r2)
        pts = np.stack([c[0] + r * np.cos(ang), c[1] + r * np.sin(ang)], 1).astype(np.int32)
        cv2.fillPoly(mask, [pts], 255, lineType=cv2.LINE_8); return pts
    if t < 0.86:      # линия / ломаная / плавная кривая, толщина 0,5..4 пикселя LR
        n = int(rng.integers(2, 6)); p = rng.uniform(-L * 0.1, L * 1.1, (n, 2))
        if n > 2 and rng.random() < 0.6:   # сгладить в кривую
            tt = np.linspace(0, 1, 60)[:, None, None]; q = np.repeat(p[None], 60, 0)     # кривая Безье (де Кастельжо)
            while q.shape[1] > 1: q = q[:, :-1] * (1 - tt) + q[:, 1:] * tt
            p = q[:, 0]
        cv2.polylines(mask, [p.astype(np.int32)], False, 255, max(1, int(rng.uniform(0.5, 4) * Mf)), lineType=cv2.LINE_8); return None
    # текст (векторные шрифты OpenCV), высота знака ~5..24 пикселя LR
    txt = "".join(chr(c) for c in rng.integers(33, 123, rng.integers(1, 9)))
    hpx = rng.uniform(5, 24) * Mf; th = max(1, int(rng.uniform(0.6, 2.5) * Mf * min(1.0, hpx / (9 * Mf)) + 1))
    org = (int(rng.uniform(-L * 0.05, L * 0.7)), int(rng.uniform(L * 0.1, L)))
    cv2.putText(mask, txt, org, int(rng.integers(0, 8)), hpx / 22.0, 255, th, lineType=cv2.LINE_8); return None


def scene(seed):
    rng = np.random.default_rng(seed); Mf = S * KS; L = CS * Mf
    t = rng.random()
    if t < 0.35: m = np.full((L, L), int(rng.integers(0, 256)), np.uint8)
    elif t < 0.6: m = gradient(rng, L)
    elif t < 0.8: m = texture(rng, L, Mf)
    else:             # триангуляция: соседние грани с близкими оттенками, как низкополигональная модель
        m = np.zeros((L, L), np.uint8); sd = cv2.Subdiv2D((-L, -L, 3 * L, 3 * L))
        for p in rng.uniform(-L * 0.3, L * 1.3, (int(rng.integers(6, 40)), 2)): sd.insert((float(p[0]), float(p[1])))
        base = rng.uniform(30, 220); var = rng.uniform(5, 120)
        for tr in sd.getTriangleList():
            cv2.fillPoly(m, [tr.reshape(3, 2).astype(np.int32)], int(np.clip(base + rng.uniform(-var, var), 0, 255)), lineType=cv2.LINE_8)
    mask = np.zeros((L, L), np.uint8)
    for _ in range(int(rng.integers(2, 13))):
        mask[:] = 0; pts = shape_mask(rng, L, Mf, mask)
        f = rng.random()
        if f < 0.62: m[mask > 0] = int(rng.integers(0, 256))
        elif f < 0.82: np.copyto(m, gradient(rng, L), where=mask > 0)
        else: np.copyto(m, texture(rng, L, Mf), where=mask > 0)
        if pts is not None and rng.random() < 0.3:    # обводка, как у спрайтов и надписей
            col = int(rng.integers(0, 60)) if rng.random() < 0.7 else int(rng.integers(0, 256))
            cv2.polylines(m, [pts], True, col, max(1, int(rng.uniform(0.7, 2.2) * Mf)), lineType=cv2.LINE_8)
    if rng.random() < 0.12:    # клетки/полосы по сетке LR (дизеринг пиксель-арта): на выходе остаются клетками
        per = int(rng.integers(1, 4)) * Mf; x0, y0 = (rng.integers(0, CS // 2, 2) * Mf); w, h = (rng.integers(6, CS // 2, 2) * Mf)
        yy, xx = grid(L); kind = rng.random()
        pat = ((yy // per + xx // per) % 2) if kind < 0.6 else ((yy // per) % 2 if kind < 0.8 else (xx // per) % 2)
        reg = np.zeros((L, L), bool); reg[y0:y0 + h, x0:x0 + w] = True
        m[reg & (pat > 0)] = int(rng.integers(0, 256))
    return pair(m, KS)


# ---------------- мастера-картинки (прореживание)
def crops_from(args):
    path_or_arr, ks, n, seed, jpeg = args
    rng = np.random.default_rng(seed)
    m0 = Yof(cv2.imread(path_or_arr, cv2.IMREAD_COLOR)) if isinstance(path_or_arr, str) else path_or_arr
    if m0 is None: return []
    out = []
    ks = [k for k in ks if min(m0.shape) >= CS * S * k and not (jpeg and k == 1)]
    if not ks: return []
    for _ in range(n):
        k = int(ks[rng.integers(len(ks))]); sz = CS * S * k
        y0 = int(rng.integers(0, m0.shape[0] - sz + 1)); x0 = int(rng.integers(0, m0.shape[1] - sz + 1))
        c = m0[y0:y0 + sz, x0:x0 + sz]
        if c.std() < 2.0 and rng.random() < 0.8: continue      # почти пустые кропы — редко
        out.append(pair(c, k))
    return out


def classical(seed):
    rng = np.random.default_rng(seed); sz = CS * S
    if rng.random() < 0.6:
        big = [q for q in PHOTO if min(q.shape) >= sz + 2]      # мелкие фото (page, text) для x4 не годятся
        p = big[rng.integers(len(big))]; f = rng.uniform(max(0.4, (sz + 2) / min(p.shape)), 1.0)
        if f < 1: p = cv2.resize(p, (max(sz, int(p.shape[1] * f)), max(sz, int(p.shape[0] * f))), interpolation=cv2.INTER_AREA)
        y0 = int(rng.integers(0, p.shape[0] - sz + 1)); x0 = int(rng.integers(0, p.shape[1] - sz + 1)); hr = p[y0:y0 + sz, x0:x0 + sz]
    else:
        t = rng.random()
        if t < 0.6:
            im = np.zeros((sz, sz), np.float32)
            for _ in range(rng.integers(2, 6)):
                g = int(rng.integers(2, sz // 3)); im += rng.uniform(0.2, 1.0) * cv2.resize(rng.random((g, g)).astype(np.float32), (sz, sz), interpolation=cv2.INTER_CUBIC)
        else:
            im = np.zeros((sz, sz), np.float32)
            for _ in range(rng.integers(5, 40)):
                p = rng.integers(0, sz, 2); cv2.circle(im, (int(p[0]), int(p[1])), int(rng.integers(2, 40)), float(rng.random()), -1, lineType=cv2.LINE_AA)
            im = cv2.GaussianBlur(im, (0, 0), float(rng.uniform(0.8, 6)))
        im = (im - im.min()) / max(float(im.max() - im.min()), 1e-6)
        hr = np.clip((im * rng.uniform(0.3, 1.0) + rng.uniform(0, 0.4)) * 255, 0, 255).astype(np.uint8)
    return cv2.resize(hr, (CS, CS), interpolation=cv2.INTER_AREA), np.ascontiguousarray(hr)


def save(name, prs):
    if not prs: print(f"{name}: пусто"); return
    lr = np.stack([p[0] for p in prs]); hr = np.stack([p[1] for p in prs])
    np.save(f"{OUT}/x{S}_{name}_lr.npy", lr); np.save(f"{OUT}/x{S}_{name}_hr.npy", hr)
    print(f"x{S}_{name}: {len(prs)} пар, LR {lr.shape[1:]}, HR {hr.shape[1:]}", flush=True)


import skimage.data as SD
PHOTO = []
for n in ("astronaut", "coffee", "chelsea", "rocket", "camera", "coins", "moon", "page", "text"):
    try:
        im = getattr(SD, n)(); PHOTO.append(Yof(cv2.cvtColor(im, cv2.COLOR_RGB2BGR)) if im.ndim == 3 else im)
    except Exception as e: print("пропуск", n, e)

if __name__ == "__main__":
    ONLY = set(sys.argv[sys.argv.index("--only") + 1].split(",")) if "--only" in sys.argv else None   # пересобрать только эти источники
    want = lambda n: ONLY is None or n in ONLY
    P = np.load(f"{A}/cap/pairs.npz"); H = f"{A}/handoff/model"
    Yrgb = lambda a: Yof(a[..., ::-1].copy())
    with Pool(14) as pool:
        if want("synth"): save("synth", pool.map(scene, range(1000, 1000 + int(N * 0.5)), chunksize=32))
        if want("synth"): save("val_synth", pool.map(scene, range(900000, 900200), chunksize=16))
        if want("photo"): save("photo", pool.map(classical, range(2000000, 2000000 + int(N * 0.12)), chunksize=64))
        # скриншоты свободных игр; игры из --val-games — только в проверку
        mf = f"{A}/dl/freeshots/manifest.json"; man = json.load(open(mf)) if os.path.exists(mf) else {}
        vg = set(sys.argv[sys.argv.index("--val-games") + 1].split(",")) if "--val-games" in sys.argv else set()
        tr = [v for v in man.values() if v["game"] not in vg]; va = [v for v in man.values() if v["game"] in vg]
        ks = (1, 2, 3) if S == 3 else (2, 3)
        if tr and want("shots"):
            per = max(1, int(N * 0.28) // len(tr) + 1)
            r = pool.map(crops_from, [(f"{A}/dl/freeshots/img/{v['file']}", ks, per, 31 + i, v["mime"] != "image/png") for i, v in enumerate(tr)])
            save("shots", [p for l in r for p in l])
        if va and want("shots"):
            r = pool.map(crops_from, [(f"{A}/dl/freeshots/img/{v['file']}", ks, 12, 77 + i, v["mime"] != "image/png") for i, v in enumerate(va)])
            save("val_shots", [p for l in r for p in l])
        print(f"скриншоты: обучение {len(tr)} файлов, проверка {len(va)} ({sorted(vg)})")
        # кадры игр владельца как мастера (s2 и sf-* — отложены)
        gm = [Yrgb(P[f"{k}-k40_f3"]) for k in ("s0", "s1", "s9")] + [Yrgb(P[f"{k}-k40_f2"]) for k in ("s0", "s1", "s9")]
        seen = set()
        for f in sorted(glob.glob(f"{H}/calib-psp/*.png")) + sorted(glob.glob(f"{H}/calib-3ds/*.png")):
            b = os.path.basename(f)
            if b.startswith("sf-") or "-s2" in b or b.startswith("prev-"): continue   # prev-*: подписи сдвинуты, prev-s9 — это сцена s2
            y = Yof(cv2.imread(f, cv2.IMREAD_UNCHANGED)); h = hashlib.md5(y.tobytes()).hexdigest()
            if h in seen: continue
            seen.add(h); gm.append(y)
        s2h = hashlib.md5(Yrgb(P["s2-k40_f1"]).tobytes()).hexdigest()   # кадр s2 под другим именем (prev-*) — убрать
        gm = [g for g in gm if hashlib.md5(g.tobytes()).hexdigest() != s2h]
        # честные рендеры с телевизора (cap/pairs4.npz, 25.11): SM3DL 4x (1600x960; s2 отложена) и N64 SM64 960x720, 1280x960
        # (пары N64 покадрово не совпали — годятся только как мастера)
        P4 = np.load(f"{A}/cap/pairs4.npz") if os.path.exists(f"{A}/cap/pairs4.npz") else None
        if P4 is not None:
            gm += [Yof(P4[k][..., ::-1].copy()) for k in P4.files if (k.endswith("_f4") or k.endswith("_f3")) and not k.startswith("s2-") and "-k3_" not in k]
        per = max(1, int(N * 0.10) // len(gm) + 1)
        if want("game"):
            r = pool.map(crops_from, [(g, (1, 2, 3) if S == 3 else (2,), per, 500 + i, False) for i, g in enumerate(gm)])
            save("game", [p for l in r for p in l])
    if S == 4 and want("real") and os.path.exists(f"{A}/cap/pairs4.npz"):   # настоящие пары 1x -> честный 4x (SM3DL s0, s1, s9; s2 отложена)
        P4 = np.load(f"{A}/cap/pairs4.npz"); rng = np.random.default_rng(6); prs = []
        ks = [k[:-3] for k in P4.files if k.endswith("_f4") and k[0] == "s" and not k.startswith("s2-") and "-k3_" not in k]
        for k in ks:
            l, h = Yof(P4[f"{k}_f1"][..., ::-1].copy()), Yof(P4[f"{k}_f4"][..., ::-1].copy())
            for _ in range(int(N * 0.05) // len(ks)):
                y0 = int(rng.integers(0, l.shape[0] - CS + 1)); x0 = int(rng.integers(0, l.shape[1] - CS + 1))
                prs.append((l[y0:y0 + CS, x0:x0 + CS].copy(), h[y0 * 4:(y0 + CS) * 4, x0 * 4:(x0 + CS) * 4].copy()))
        save("real", prs); print("настоящие пары x4:", ks)
    if S == 3 and want("real"):   # настоящие пары 1x -> честный 3x
        rng = np.random.default_rng(5); prs = []
        for k in ("s0", "s9"):
            l, h = Yrgb(P[f"{k}-k40_f1"]), Yrgb(P[f"{k}-k40_f3"])
            for _ in range(int(N * 0.03) // 2):
                y0 = int(rng.integers(0, l.shape[0] - CS + 1)); x0 = int(rng.integers(0, l.shape[1] - CS + 1))
                prs.append((l[y0:y0 + CS, x0:x0 + CS].copy(), h[y0 * 3:(y0 + CS) * 3, x0 * 3:(x0 + CS) * 3].copy()))
        save("real", prs)
    if S == 3 and want("real4") and os.path.exists(f"{A}/cap/pairs4.npz"):
        # настоящий вход 1x, цель — честный 4x, уменьшенный по площади до 3x (слегка сглаженный): новые сцены SM3DL с телевизора
        P4 = np.load(f"{A}/cap/pairs4.npz"); rng = np.random.default_rng(7); prs = []
        ks = [k[:-3] for k in P4.files if k.endswith("_f4") and k[0] == "s" and not k.startswith("s2-") and "-k3_" not in k]
        for k in ks:
            l = Yrgb(P4[f"{k}_f1"]); h = cv2.resize(Yrgb(P4[f"{k}_f4"]), (l.shape[1] * 3, l.shape[0] * 3), interpolation=cv2.INTER_AREA)
            for _ in range(int(N * 0.05) // len(ks)):
                y0 = int(rng.integers(0, l.shape[0] - CS + 1)); x0 = int(rng.integers(0, l.shape[1] - CS + 1))
                prs.append((l[y0:y0 + CS, x0:x0 + CS].copy(), h[y0 * 3:(y0 + CS) * 3, x0 * 3:(x0 + CS) * 3].copy()))
        save("real4", prs)
