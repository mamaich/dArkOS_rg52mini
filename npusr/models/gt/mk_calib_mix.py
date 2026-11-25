#!/usr/bin/env python3
"""Калибровочный набор calib-mix для моделей, обученных по настоящей картинке (GLES: PS1, 16 бит, N64, Saturn; 3DS, PSP).
Диапазоны активаций INT8 toolkit берёт как min/max по этим кадрам, поэтому в наборе должны быть и резкие контрастные
картинки (пиксель-арт), а не только кадры SM3DL. Состав (Y, PNG):
  - calib-3ds и calib-psp без дублей;
  - родные кадры 320x240 с консоли (cap/native: PS1 DD, HR; N64 SM64; Saturn RE);
  - родные кадры 16-битных игр со снимков dArkOS (cap/native/16-*: MD, 32X, SNES, NES, PCE, Neo Geo, PS1 2D);
  - 6 листов 320x256 из входов синтетики (train/gt/x4_synth_lr.npy) — резкие края, текст, клетки;
  - 2 листа из прореженных скриншотов свободных игр, если они есть.
usage: mk_calib_mix.py -> handoff/model/calib-mix/"""
import glob, os, hashlib
import numpy as np, cv2
A = "/home/mamaich/rg52/aisr"; O = f"{A}/handoff/model/calib-mix"; os.makedirs(O, exist_ok=True)
for f in glob.glob(f"{O}/*.png"): os.remove(f)
Yof = lambda im: cv2.cvtColor(im, cv2.COLOR_BGR2YCrCb)[..., 0] if im.ndim == 3 else im
seen = set(); n = 0


def put(name, y):
    global n
    h = hashlib.md5(np.ascontiguousarray(y).tobytes()).hexdigest()
    if h in seen: return
    seen.add(h); cv2.imwrite(f"{O}/{name}.png", y); n += 1


for d in ("calib-3ds", "calib-psp"):
    for f in sorted(glob.glob(f"{A}/handoff/model/{d}/*.png")):
        put(f"{d[6:]}-{os.path.basename(f)[:-4]}", Yof(cv2.imread(f, cv2.IMREAD_UNCHANGED)))
for lab in ("ps1c-dd-1x", "ps1c-hr-1x", "satc-re-r3", "n64c-320"):
    put(f"native-{lab}", Yof(cv2.imread(f"{A}/cap/native/{lab}.png")))
for f in sorted(glob.glob(f"{A}/cap/native/16-*.png")):     # 16-битные игры и PS1 2D со снимков dArkOS (native16.py)
    put(f"native-{os.path.basename(f)[:-4]}", Yof(cv2.imread(f)))
for name, cnt in (("synth", 6), ("shots", 2)):
    f = f"{A}/train/gt/x4_{name}_lr.npy"
    if not os.path.exists(f): continue
    lr = np.load(f)
    for k in range(cnt):
        t = lr[k * 20:(k + 1) * 20]
        if len(t) < 20: break
        put(f"{name}-{k}", np.vstack([np.hstack(list(t[j:j + 5])) for j in range(0, 20, 5)]))
print(f"{O}: {n} кадров")
