#!/usr/bin/env python3
"""Сеть s2d (та же, что в s2d_distill2.py: вход space-to-depth 2x2 яркости, C каналов, NB свёрток 3x3 + ReLU, выход (2S)^2
каналов до pixel shuffle, Clip [0,1]), но обучение ПО НАСТОЯЩЕЙ КАРТИНКЕ высокого разрешения, без учителя ESPCN.
Пары готовит sr_data.py (train/gt/x<S>_*.npy): синтетика, скриншоты свободных игр, кадры игр владельца, фото.
Проверка: отложенная синтетика, отложенные скриншоты, SM3DL сцена s2 (в обучении нет):
  S=3 — к честному рендеру 3x; S=4 — прокси: выход, уменьшенный до 3x и до 2x, к честным 3x и 2x.
usage: s2d_gt.py --scale 4 --blocks 9 --init-bicubic --tag x4_b9_gt
       s2d_gt.py --scale 4 --blocks 9 --init s2d/x4_b9_gt.pt --qat --qnoise --calib-dir <кадры> --lr 2e-4 --steps 30000 --tag x4_b9_gtn"""
import argparse, glob, os, time
import numpy as np, cv2, torch, torch.nn as nn, torch.nn.functional as F

ap = argparse.ArgumentParser()
ap.add_argument("--scale", type=int, required=True)
ap.add_argument("--ch", type=int, default=32)
ap.add_argument("--blocks", type=int, default=7)
ap.add_argument("--steps", type=int, default=80000)
ap.add_argument("--batch", type=int, default=96)
ap.add_argument("--lr", type=float, default=1e-3)
ap.add_argument("--init", default=None, help="начальные веса — свой чекпойнт .pt")
ap.add_argument("--init-bicubic", action="store_true", help="старт с «bicubic-тождества» (без чужих весов)")
ap.add_argument("--qat", action="store_true"); ap.add_argument("--qnoise", action="store_true")
ap.add_argument("--calib-dir", default=None, help="QAT: диапазоны активаций по этим кадрам (как калибровка toolkit)")
ap.add_argument("--shares", default="synth=0.45,shots=0.25,game=0.12,photo=0.12,real=0.06", help="доли источников (нет файла — доля делится между остальными)")
ap.add_argument("--dither", type=float, default=0.12, help="доля входов с дизерингом PS1 (5 бит + матрица 4x4); цель — чистая")
ap.add_argument("--actcap", type=float, default=0.0, help="штраф за активации скрытых слоёв выше этого значения (0 — нет): уже диапазоны INT8")
ap.add_argument("--capw", type=float, default=1.0)
ap.add_argument("--tag", required=True)
a = ap.parse_args()
S, C, NB = a.scale, a.ch, a.blocks; CAP = a.actcap
KI = 2; R = KI * S
OUT = "/home/mamaich/rg52/aisr/train/s2d"; GT = "/home/mamaich/rg52/aisr/train/gt"; tag = a.tag
dev = "cuda"; torch.manual_seed(0)


class FQ(nn.Module):
    """фейк-квантование активации как у RKNN: per-tensor, асимметрично, 256 уровней; диапазон задаёт калибровка"""
    def __init__(self):
        super().__init__()
        self.register_buffer("lo", torch.zeros(())); self.register_buffer("hi", torch.zeros(())); self.register_buffer("n", torch.zeros(()))
        self.frozen = False; self.noise = False

    def forward(self, x):
        if self.training and not self.frozen:
            lo, hi = x.detach().min(), x.detach().max()
            if self.n == 0: self.lo.copy_(lo); self.hi.copy_(hi)
            else: self.lo.mul_(0.99).add_(0.01 * lo); self.hi.mul_(0.99).add_(0.01 * hi)
            self.n += 1
        lo = torch.clamp(self.lo, max=0.0); hi = torch.maximum(self.hi, lo + 1e-6)
        sc = (hi - lo) / 255; zp = torch.round(-lo / sc)
        if self.noise and self.training:
            return torch.clamp(x, lo, hi) + (torch.rand_like(x) - 0.5) * sc
        q = (torch.clamp(torch.round(x / sc) + zp, 0, 255) - zp) * sc
        return x + (q - x).detach()


def fqw(w):
    sc = w.detach().abs().amax(dim=(1, 2, 3), keepdim=True).clamp_min(1e-8) / 127
    return w + (torch.round(w / sc).clamp(-127, 127) * sc - w).detach()


class Student(nn.Module):
    def __init__(self):
        super().__init__()
        self.c = nn.ModuleList([nn.Conv2d(KI * KI, C, 3, padding=1)] + [nn.Conv2d(C, C, 3, padding=1) for _ in range(NB)] + [nn.Conv2d(C, R * R, 3, padding=1)])
        self.qat = False
        self.fq = nn.ModuleList([FQ() for _ in range(NB + 2)])

    def conv(self, k, x):
        cv = self.c[k]
        if not self.qat: return cv(x)
        w = cv.weight
        if self.fq[0].noise and self.training:
            sc = w.detach().abs().amax(dim=(1, 2, 3), keepdim=True).clamp_min(1e-8) / 127
            return F.conv2d(x, w + (torch.rand_like(w) - 0.5) * sc, cv.bias, padding=1)
        return F.conv2d(x, fqw(w), cv.bias, padding=1)

    def core(self, x, clamp=True):
        q = (lambda k, t: self.fq[k](t)) if self.qat else (lambda k, t: t)
        self.pen = 0.0

        def cap(t):   # штраф за выбросы активаций выше CAP: диапазон INT8 = min/max по калибровке, редкие выбросы огрубляют шаг
            if CAP > 0 and self.training:
                e = F.relu(t.abs() - CAP); self.pen = self.pen + e.pow(2).mean() + 0.02 * e.amax()
            return t
        x = q(0, cap(self.conv(0, x)))
        for k in range(1, NB + 1): x = q(k, cap(F.relu(self.conv(k, x))))
        x = self.conv(NB + 1, x)
        if self.qat: return q(NB + 1, x.clamp(0, 1))
        return x.clamp(0, 1) if clamp else x

    @torch.no_grad()
    def calibrate(self, frames):
        was, self.qat = self.qat, False
        lo = [float("inf")] * (NB + 2); hi = [-float("inf")] * (NB + 2)
        for y in frames:
            x = F.pixel_unshuffle(y, KI)
            t = [self.c[0](x)]
            for k in range(1, NB + 1): t.append(F.relu(self.c[k](t[-1])))
            t.append(self.c[-1](t[-1]).clamp(0, 1))
            for k, v in enumerate(t): lo[k] = min(lo[k], v.min().item()); hi[k] = max(hi[k], v.max().item())
        for k, f in enumerate(self.fq):
            f.lo.fill_(lo[k]); f.hi.fill_(hi[k]); f.n.fill_(1); f.frozen = True
        self.qat = was
        return lo, hi

    def forward(self, y, clamp=True):
        return F.pixel_shuffle(self.core(F.pixel_unshuffle(y, KI), clamp), R)


st = Student().to(dev)
if a.init:
    st.load_state_dict(torch.load(a.init, map_location=dev), strict=False); print("начальные веса:", a.init)
st.qat = a.qat
if a.qnoise:
    for f in st.fq: f.noise = True
if a.init_bicubic:
    def cub(x, A=-0.75):
        x = abs(x)
        return (A + 2) * x ** 3 - (A + 3) * x ** 2 + 1 if x <= 1 else (A * x ** 3 - 5 * A * x ** 2 + 8 * A * x - 4 * A if x < 2 else 0.0)
    with torch.no_grad():
        for c in st.c:
            c.weight.normal_(0, 1e-2); c.bias.zero_()
        NI = KI * KI
        for ch in range(NI):
            for k in range(NB + 1):
                st.c[k].weight[ch, :, :, :] = 0; st.c[k].weight[ch, ch, 1, 1] = 1.0
        T = st.c[-1].weight; T[:, NI:] *= 0.1; T[:, :NI] = 0
        for dy in range(R):
            ty = (dy + 0.5) * KI / R - 0.5
            for dx in range(R):
                tx = (dx + 0.5) * KI / R - 0.5
                for kh in range(3):
                    for py in range(KI):
                        wy = cub(ty - (KI * (kh - 1) + py))
                        if wy == 0: continue
                        for kw in range(3):
                            for px in range(KI):
                                T[dy * R + dx, py * KI + px, kh, kw] += wy * cub(tx - (KI * (kw - 1) + px))
    print("начальные веса: bicubic-тождество")
print(f"{tag}: параметров {sum(p.numel() for p in st.c.parameters())}, x{S}, блоков {NB}, каналов {C}", flush=True)

# ---------------- данные
SH = dict(kv.split("=") for kv in a.shares.split(","))
POOL = {}
for n, sh in SH.items():
    f = f"{GT}/x{S}_{n}_lr.npy"
    if not os.path.exists(f) or float(sh) <= 0: continue
    POOL[n] = (torch.from_numpy(np.load(f)).to(dev), torch.from_numpy(np.load(f"{GT}/x{S}_{n}_hr.npy")).to(dev), float(sh))
tot = sum(v[2] for v in POOL.values())
print("источники:", {n: (len(v[0]), round(v[2] / tot, 3)) for n, v in POOL.items()}, flush=True)
NAMES = list(POOL); PROB = torch.tensor([POOL[n][2] / tot for n in NAMES], device=dev)
DM = torch.tensor([[-4, 0, -3, 1], [2, -2, 3, -1], [-3, 1, -4, 0], [3, -1, 2, -2]], dtype=torch.float32, device=dev)   # матрица дизеринга PS1


def batch():
    B = a.batch
    src = torch.multinomial(PROB, B, replacement=True)
    ls, hs = [], []
    for k, n in enumerate(NAMES):
        c = int((src == k).sum())
        if not c: continue
        lr, hr, _ = POOL[n]; idx = torch.randint(len(lr), (c,), device=dev)
        ls.append(lr[idx]); hs.append(hr[idx])
    x = torch.cat(ls).float(); y = torch.cat(hs).float() / 255          # x в 0..255
    for dims in ((2,), (1,)):
        f = torch.rand(B, 1, 1, device=dev) < 0.5
        x = torch.where(f, x.flip(dims), x); y = torch.where(f, y.flip(dims), y)
    f = torch.rand(B, 1, 1, device=dev) < 0.5
    x = torch.where(f, x.transpose(1, 2), x); y = torch.where(f, y.transpose(1, 2), y)
    # яркость/контраст без отсечения (отсечение не перестановочно с усреднением краёв): сжатие диапазона и инверсия
    full = torch.rand(B, 1, 1, device=dev) < 0.5
    lo = torch.rand(B, 1, 1, device=dev) * 0.4; hi = lo + 0.25 + torch.rand(B, 1, 1, device=dev) * (0.75 - lo)
    lo = torch.where(full, torch.zeros_like(lo), lo); hi = torch.where(full, torch.ones_like(hi), hi.clamp(max=1))
    inv = torch.rand(B, 1, 1, device=dev) < 0.3
    lo, hi = torch.where(inv, hi, lo), torch.where(inv, lo, hi)
    x = lo * 255 + x * (hi - lo); y = lo + y * (hi - lo)
    x = x.round()
    if a.dither > 0:
        d = torch.rand(B, 1, 1, device=dev) < a.dither
        v5 = torch.floor((x + DM.repeat(x.shape[1] // 4, x.shape[2] // 4)[None]) / 8).clamp(0, 31)
        x = torch.where(d, v5 * 8 + torch.floor(v5 / 4), x)
    return (x / 255)[:, None], y[:, None]


psnr = lambda a_, b_: 10 * torch.log10(1 / F.mse_loss(a_.double(), b_.double())).item()
VAL = {}
for n in ("val_synth", "val_shots"):
    f = f"{GT}/x{S}_{n}_lr.npy"
    if os.path.exists(f): VAL[n] = (torch.from_numpy(np.load(f)).to(dev).float()[:, None] / 255, torch.from_numpy(np.load(f"{GT}/x{S}_{n}_hr.npy")).to(dev).float()[:, None] / 255)
P = np.load("/home/mamaich/rg52/aisr/cap/pairs.npz")
Yrgb = lambda im: cv2.cvtColor(im[..., ::-1].copy(), cv2.COLOR_BGR2YCrCb)[..., 0]
T8 = lambda v: torch.from_numpy(v.astype(np.float32) / 255).to(dev)[None, None]
SM = {k: (T8(Yrgb(P[f"{k}-k40_f1"])), T8(Yrgb(P[f"{k}-k40_f2"])), T8(Yrgb(P[f"{k}-k40_f3"]))) for k in ("s0", "s2", "s9")}
# честные пары 1x -> 4x с телевизора (cap/pairs4.npz): s2 — отложена (в обучении нет), остальные сцены — в обучении
R4 = {}
if S == 4 and os.path.exists("/home/mamaich/rg52/aisr/cap/pairs4.npz"):
    P4 = np.load("/home/mamaich/rg52/aisr/cap/pairs4.npz")
    R4 = {k[:-3]: (T8(Yrgb(P4[k[:-3] + "_f1"])), T8(Yrgb(P4[k]))) for k in P4.files if k.endswith("_f4") and k[0] == "s" and "-k3_" not in k}
CAL = []
if a.calib_dir:
    for f in sorted(glob.glob(os.path.join(a.calib_dir, "*.png"))):
        yy = cv2.imread(f, cv2.IMREAD_UNCHANGED); yy = cv2.cvtColor(yy, cv2.COLOR_BGR2YCrCb)[..., 0] if yy.ndim == 3 else yy
        h_, w_ = yy.shape[0] // KI * KI, yy.shape[1] // KI * KI
        CAL.append(torch.from_numpy(yy[:h_, :w_].astype(np.float32) / 255).to(dev)[None, None])
    print("калибровочных кадров", len(CAL))


def sm3dl(fn):
    """SM3DL: S=3 — к честному 3x; S=4 — прокси B (вниз до 3x) и C (вниз до 2x). Возвращает {сцена: (..)}"""
    r = {}
    for k, (y1, g2, g3) in SM.items():
        o = fn(y1)
        if S == 3: r[k] = (psnr(o[..., 12:-12, 12:-12], g3[..., 12:-12, 12:-12]),)
        elif S == 4:
            b = F.interpolate(o, size=g3.shape[-2:], mode="area"); c = F.avg_pool2d(o, 2)
            r[k] = (psnr(b[..., 12:-12, 12:-12], g3[..., 12:-12, 12:-12]), psnr(c[..., 8:-8, 8:-8], g2[..., 8:-8, 8:-8]))
        else: r[k] = (psnr(o[..., 8:-8, 8:-8], g2[..., 8:-8, 8:-8]),)
    if R4:   # к честному 4x: s2 (отложена) и среднее по сценам из обучения
        q = {k: psnr(fn(y1)[..., 16:-16, 16:-16], g4[..., 16:-16, 16:-16]) for k, (y1, g4) in R4.items()}
        r["честн4x:s2"] = (float(np.mean([v for k, v in q.items() if k.startswith("s2-")])),)
        r["обуч"] = (float(np.mean([v for k, v in q.items() if not k.startswith("s2-")])),)
    return r


def evaluate():
    st.eval(); v = {}
    with torch.no_grad():
        for n, (lr, hr) in VAL.items():
            e = [F.mse_loss(st(lr[i:i + 50]).double(), hr[i:i + 50].double()).item() for i in range(0, len(lr), 50)]
            v[n] = 10 * np.log10(1 / np.mean(e))
        sm = sm3dl(st)
    st.train(); return v, sm


bic = lambda y: F.interpolate(y, scale_factor=S, mode="bicubic", align_corners=False).clamp(0, 1)
with torch.no_grad():
    vb = {n: 10 * np.log10(1 / np.mean([F.mse_loss(bic(lr[i:i + 50]).double(), hr[i:i + 50].double()).item() for i in range(0, len(lr), 50)])) for n, (lr, hr) in VAL.items()}
    print("бикубика:", {n: round(x, 2) for n, x in vb.items()}, "SM3DL", {k: tuple(round(x, 2) for x in v) for k, v in sm3dl(bic).items()}, flush=True)
fmt = lambda v, sm: " ".join(f"{n} {x:.2f}" for n, x in v.items()) + " | SM3DL " + " ".join(f"{k} " + "/".join(f"{x:.2f}" for x in t) for k, t in sm.items())
v, sm = evaluate(); print("старт:", fmt(v, sm), flush=True)
opt = torch.optim.Adam(st.c.parameters(), lr=a.lr)
sched = torch.optim.lr_scheduler.OneCycleLR(opt, max_lr=a.lr, total_steps=a.steps, pct_start=0.05)
keep = lambda: {k: v for k, v in st.state_dict().items() if not k.startswith("fq.")}
t0 = time.time(); best = -1
for it in range(1, a.steps + 1):
    if a.qat and a.calib_dir and (it - 1) % 2500 == 0:
        lo_, hi_ = st.calibrate(CAL)
        if it == 1: print("диапазоны (калибровка):", [f"{l:.2f}..{h:.2f}" for l, h in zip(lo_, hi_)], flush=True)
    if a.qat and it == 1:
        v, sm = evaluate(); print("QAT, до обучения:", fmt(v, sm), flush=True)
    x, y = batch()
    out = st(x, clamp=False)
    loss = F.l1_loss(out, y) + 4 * F.mse_loss(out, y)
    if CAP > 0: loss = loss + a.capw * st.pen
    opt.zero_grad(); loss.backward(); opt.step(); sched.step()
    if it % 2500 == 0 or it == a.steps:
        v, sm = evaluate(); sc = float(np.mean(list(v.values())))
        if sc > best: best = sc; torch.save(keep(), f"{OUT}/{tag}.pt")
        print(f"  шаг {it}: loss {loss.item():.5f}, {fmt(v, sm)}, {time.time() - t0:.0f} с", flush=True)
torch.save(keep(), f"{OUT}/{tag}_last.pt")
if a.calib_dir:
    lo_, hi_ = st.calibrate(CAL); print("диапазоны в конце:", [f"{l:.2f}..{h:.2f}" for l, h in zip(lo_, hi_)], flush=True)
print(f"лучшее (отложенные) {best:.2f} dB -> {OUT}/{tag}.pt; последний -> {tag}_last.pt")
