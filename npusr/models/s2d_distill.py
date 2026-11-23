#!/usr/bin/env python3
"""Trains a space-to-depth model for NPU UPSCALE by distillation: the student learns to give what ESPCN xS
(the teacher, ESPCN_x<S>.pb from TF-ESPCN, Apache-2.0) gives on the same frames; no high-resolution frames are
targets. The student: space-to-depth 2x2 of the luma in (4 channels), conv 3x3 4->C, NB x (conv 3x3 C->C, ReLU),
conv 3x3 C->(2S)^2, Clip [0,1], pixel shuffle out. s2d_rknn.py turns its weights into the .rknn.

Its inputs: frames of the systems' games (not in the sources), scikit-image's sample photographs (skimage.data),
and shapes, text, chequers, gradients and textures drawn by this script. A pool of crops on the GPU, renewed
every 10k steps. Checked on frames kept out of training: against the teacher (PSNR), and against an honest
render at S times the resolution (the student's and the teacher's).

The PSP's model (s2d_x2_v7.pt beside this script; trained 2026-11-23 on an RTX 5070):
  python s2d_distill.py --scale 2 --steps 60000 --shares 0.45,0.15,0.40 --init-bicubic --tag v6_x2_bicubic ...
  python s2d_distill.py --scale 2 --steps 60000 --lr 4e-4 --shares 0.45,0.15,0.40 --init out/v6_x2_bicubic.pt \
      --tag v7_x2_bicubic ...
  (... = --psp DIR --3ds DIR --pairs FILE). The second run was cut short at step 42500 by the PC's power-off; the
  best checkpoint saved by then is the model. Training on a GPU is not bit-exact, hence the weights are kept.

  --psp    PSP frames, 480x272 png: the native frame taken from PPSSPP's 2x nearest output on the console. Used for
           the model: Daxter (8), Final Fantasy Anniversary (6); files named sf-* (Street Fighter Alpha 3 Max, 6)
           are checks only.
  --3ds    3DS frames, Y 400x240 png (Super Mario 3D Land, 14); files with -s2 in the name are checks only.
  --pairs  npz of Super Mario 3D Land renders of the same view at 1x, 2x, 3x ("<scene>-k40_f<factor>", RGB):
           scenes s0, s1, s9 go into training (2x and 3x renders downscaled), s2 is the honest-render check.
  --init-bicubic  start from a "bicubic identity": the first convolution copies the 4 space-to-depth channels,
           the body passes them through, the last convolution is the bicubic kernel (Keys, a=-0.75) over 3x3 cells;
           every other weight normal noise, sigma 0.01, seed 0. No other network's weights."""
import argparse, glob, os, time
import numpy as np, cv2, torch, torch.nn as nn, torch.nn.functional as F

ap = argparse.ArgumentParser()
ap.add_argument("--scale", type=int, required=True)
ap.add_argument("--ch", type=int, default=32)
ap.add_argument("--blocks", type=int, default=5)
ap.add_argument("--steps", type=int, default=40000)
ap.add_argument("--batch", type=int, default=64)
ap.add_argument("--crop", type=int, default=96)
ap.add_argument("--lr", type=float, default=1e-3)
ap.add_argument("--init", default=None, help="start weights: a checkpoint of this script (.pt), to train on")
ap.add_argument("--init-bicubic", action="store_true", help="start from the bicubic identity (no other network's weights)")
ap.add_argument("--shares", default="0.45,0.35,0.20", help="shares of the crops: games, photographs, synthetic")
ap.add_argument("--margin", type=int, default=0, help="leave this many output pixels at a crop's edges out of the loss")
ap.add_argument("--psp", required=True); ap.add_argument("--3ds", dest="n3ds", required=True); ap.add_argument("--pairs", required=True)
ap.add_argument("--pb", default=None, help="the teacher; default: ESPCN_x<S>.pb beside this script")
ap.add_argument("--out", default="out")
ap.add_argument("--tag", default=None)
a = ap.parse_args()
S, C, NB, R, CS = a.scale, a.ch, a.blocks, 2 * a.scale, a.crop
tag = a.tag or f"s2d_x{S}_c{C}_b{NB}"
OUT = os.path.abspath(a.out); os.makedirs(OUT, exist_ok=True)
dev = "cuda" if torch.cuda.is_available() else "cpu"; torch.manual_seed(0); rng = np.random.default_rng(0)

pb = a.pb or os.path.join(os.path.dirname(os.path.abspath(__file__)), f"ESPCN_x{S}.pb")
net = cv2.dnn.readNetFromTensorflow(pb)
gp = lambda n: torch.from_numpy(net.getParam(net.getLayerId(n), 0).copy()).to(dev)
TW = [gp("conv1"), gp("conv2"), gp("conv3")]; TB = [gp("add").reshape(-1), gp("add_1").reshape(-1), gp("add_2").reshape(-1)]


def teacher(y):
    x = F.relu(F.conv2d(y, TW[0], TB[0], padding=2)); x = F.relu(F.conv2d(x, TW[1], TB[1], padding=1))
    return F.pixel_shuffle(torch.tanh(F.conv2d(x, TW[2], TB[2], padding=1)), S).clamp(0, 1)


class Student(nn.Module):
    def __init__(self):
        super().__init__()
        self.c = nn.ModuleList([nn.Conv2d(4, C, 3, padding=1)] + [nn.Conv2d(C, C, 3, padding=1) for _ in range(NB)] + [nn.Conv2d(C, R * R, 3, padding=1)])

    def core(self, x, clamp=True):   # space-to-depth in, R*R channels out: what goes into the .rknn (with the Clip)
        x = self.c[0](x)
        for k in range(1, NB + 1): x = F.relu(self.c[k](x))
        x = self.c[NB + 1](x)
        return x.clamp(0, 1) if clamp else x

    def forward(self, y, clamp=True):   # trained without the Clip: from a random start it would stop the gradient
        return F.pixel_shuffle(self.core(F.pixel_unshuffle(y, 2), clamp), R)


st = Student().to(dev)
if a.init:
    st.load_state_dict(torch.load(a.init, map_location=dev), strict=False); print("start weights:", a.init)
if a.init_bicubic:
    # The first convolution copies the 4 space-to-depth channels, the body passes them through (Y >= 0, so the
    # ReLU does not touch them), the last one is bicubic (Keys, a=-0.75 as in cv2 and PyTorch) over 3x3 cells =
    # 6x6 native pixels; the rest small noise.
    def cub(x, A=-0.75):
        x = abs(x)
        return (A + 2) * x ** 3 - (A + 3) * x ** 2 + 1 if x <= 1 else (A * x ** 3 - 5 * A * x ** 2 + 8 * A * x - 4 * A if x < 2 else 0.0)
    with torch.no_grad():
        for c in st.c:
            c.weight.normal_(0, 1e-2); c.bias.zero_()
        for ch in range(4):
            for k in range(NB + 1):
                st.c[k].weight[ch, :, :, :] = 0; st.c[k].weight[ch, ch, 1, 1] = 1.0
        T = st.c[-1].weight; T[:, 4:] *= 0.1; T[:, :4] = 0
        for dy in range(R):
            ty = (dy + 0.5) / S - 0.5            # the output row in native pixels from the cell's first row
            for dx in range(R):
                tx = (dx + 0.5) / S - 0.5
                for kh in range(3):
                    for py in range(2):
                        wy = cub(ty - (2 * (kh - 1) + py))
                        if wy == 0: continue
                        for kw in range(3):
                            for px in range(2):
                                T[dy * R + dx, py * 2 + px, kh, kw] += wy * cub(tx - (2 * (kw - 1) + px))
    print("start weights: the bicubic identity")
print(f"{tag}: {sum(p.numel() for p in st.parameters())} weights, {4*C*9 + NB*C*C*9 + C*R*R*9} MAC per space-to-depth cell", flush=True)

# ---------------- the inputs
Yof = lambda im: cv2.cvtColor(im, cv2.COLOR_BGR2YCrCb)[..., 0] if im.ndim == 3 else im
game, val = [], []
for f in sorted(glob.glob(f"{a.psp}/*.png")):
    (val if os.path.basename(f).startswith("sf-") else game).append(Yof(cv2.imread(f)))
for f in sorted(glob.glob(f"{a.n3ds}/*.png")):
    (val if "-s2" in os.path.basename(f) else game).append(cv2.imread(f, cv2.IMREAD_GRAYSCALE))
P = np.load(a.pairs)
for k in ("s0", "s1", "s9"):
    for fct, d in (("f2", 2), ("f3", 3), ("f3", 2), ("f2", 1.5)):
        im = Yof(P[f"{k}-k40_{fct}"][..., ::-1].copy())
        game.append(cv2.resize(im, (int(im.shape[1] / d), int(im.shape[0] / d)), interpolation=cv2.INTER_AREA))
photo = []
import skimage.data as SD
# public domain or CC0 by their notes in skimage.data; moon and page have none and come with scikit-image (BSD-3-Clause)
for n in ("astronaut", "coffee", "chelsea", "rocket", "camera", "coins", "moon", "page", "text"):
    try:
        im = getattr(SD, n)(); y = Yof(cv2.cvtColor(im, cv2.COLOR_RGB2BGR)) if im.ndim == 3 else im
        photo.append(y); photo.append(cv2.resize(y, (y.shape[1] // 2, y.shape[0] // 2), interpolation=cv2.INTER_AREA))
    except Exception as e: print("skipped", n, e)


def synth(n=400, sz=192):
    out = []
    for _ in range(n):
        im = np.full((sz, sz, 3), rng.integers(0, 256, 3), np.uint8)
        if rng.random() < 0.4:   # a gradient behind
            g = np.linspace(0, 1, sz)[None, :, None] if rng.random() < 0.5 else np.linspace(0, 1, sz)[:, None, None]
            im = np.clip(im * (0.5 + g), 0, 255).astype(np.uint8)
        for _ in range(rng.integers(3, 14)):
            col = tuple(int(c) for c in rng.integers(0, 256, 3)); t = rng.random()
            if t < 0.35:
                pts = rng.integers(-20, sz + 20, (rng.integers(3, 7), 2)).astype(np.int32)
                cv2.fillPoly(im, [pts], col, lineType=cv2.LINE_8)
            elif t < 0.55:
                p = rng.integers(0, sz, 2); cv2.ellipse(im, (int(p[0]), int(p[1])), (int(rng.integers(3, 60)), int(rng.integers(3, 60))),
                                                       float(rng.uniform(0, 180)), 0, 360, col, -1, lineType=cv2.LINE_8)
            elif t < 0.7:
                p = rng.integers(0, sz, 4); cv2.line(im, (int(p[0]), int(p[1])), (int(p[2]), int(p[3])), col, int(rng.integers(1, 4)), lineType=cv2.LINE_8)
            elif t < 0.85:
                txt = "".join(chr(c) for c in rng.integers(48, 91, rng.integers(2, 9)))
                org = (int(rng.integers(0, sz // 2)), int(rng.integers(10, sz))); fnt = int(rng.integers(0, 8))
                fs = float(rng.uniform(0.3, 1.5)); th = int(rng.integers(1, 3))
                if rng.random() < 0.5:   # outlined text, as in games' captions and icons
                    cv2.putText(im, txt, org, fnt, fs, (0, 0, 0) if rng.random() < 0.7 else (255, 255, 255), th + 2, lineType=cv2.LINE_8)
                cv2.putText(im, txt, org, fnt, fs, col, th, lineType=cv2.LINE_8)
            else:
                p = rng.integers(0, sz, 2); w = rng.integers(10, 80, 2); per = int(rng.integers(1, 6))
                yy, xx = np.mgrid[0:w[1], 0:w[0]]
                m = ((yy // per + xx // per) % 2).astype(bool)
                y0, x0 = int(p[1]), int(p[0]); reg = im[y0:y0 + w[1], x0:x0 + w[0]]
                reg[m[:reg.shape[0], :reg.shape[1]]] = col
        out.append(Yof(im))
    return out


def textures(n=300, sz=192):
    """procedural "natural" textures in place of photographs: multi-octave noise, blurred blobs, gratings"""
    out = []
    for _ in range(n):
        t = rng.random()
        if t < 0.5:
            im = np.zeros((sz, sz), np.float32)
            for _ in range(rng.integers(2, 6)):
                g = int(rng.integers(2, 48))
                im += rng.uniform(0.2, 1.0) * cv2.resize(rng.random((g, g)).astype(np.float32), (sz, sz), interpolation=cv2.INTER_CUBIC)
        elif t < 0.75:
            im = np.zeros((sz, sz), np.float32)
            for _ in range(rng.integers(5, 40)):
                p = rng.integers(0, sz, 2); cv2.circle(im, (int(p[0]), int(p[1])), int(rng.integers(2, 30)), float(rng.random()), -1)
            im = cv2.GaussianBlur(im, (0, 0), float(rng.uniform(0.5, 6)))
        else:
            yy, xx = np.mgrid[0:sz, 0:sz].astype(np.float32); th = rng.uniform(0, np.pi); f = rng.uniform(0.02, 0.6)
            im = 0.5 + 0.5 * np.sin((xx * np.cos(th) + yy * np.sin(th)) * f * 2 * np.pi + rng.uniform(0, 6.28))
        im = (im - im.min()) / max(float(im.max() - im.min()), 1e-6)
        im = im * rng.uniform(0.3, 1.0) + rng.uniform(0, 0.4)
        out.append(np.clip(im * 255, 0, 255).astype(np.uint8))
    return out


print(f"game frames {len(game)}, photographs {len(photo)}, checks {len(val)}", flush=True)
SH = [float(x) for x in a.shares.split(",")]


def pool(K=30000):
    srcs = [(game, SH[0]), (photo, SH[1]), (synth() + textures(), SH[2])]
    crops = []
    for lst, share in srcs:
        lst = [t for t in lst if t.shape[0] >= CS and t.shape[1] >= CS]
        for _ in range(int(K * share)):
            t = lst[rng.integers(len(lst))]
            y0 = rng.integers(0, t.shape[0] - CS + 1); x0 = rng.integers(0, t.shape[1] - CS + 1)
            crops.append(t[y0:y0 + CS, x0:x0 + CS])
    return torch.from_numpy(np.stack(crops)).to(dev)


def batch(pl):
    x = pl[torch.randint(len(pl), (a.batch,), device=dev)].float() / 255
    B = a.batch
    f1 = torch.rand(B, 1, 1, device=dev) < 0.5; x = torch.where(f1, x.flip(2), x)
    f2 = torch.rand(B, 1, 1, device=dev) < 0.5; x = torch.where(f2, x.flip(1), x)
    f3 = torch.rand(B, 1, 1, device=dev) < 0.5; x = torch.where(f3, x.transpose(1, 2), x)
    g = torch.empty(B, 1, 1, device=dev).uniform_(0.6, 1.4); b = torch.empty(B, 1, 1, device=dev).uniform_(-0.15, 0.15)
    x = (((x - 0.5) * g + 0.5 + b).clamp(0, 1) * 255).round() / 255
    return x[:, None]


psnr = lambda a_, b_: 10 * torch.log10(1 / F.mse_loss(a_.double(), b_.double())).item()
MV = 8 * S   # the edges (output pixels) left out of the check
EDGE = []    # PSNR over the whole frame, edges included (the latest)


def evaluate():
    st.eval(); r = []; rf = []
    with torch.no_grad():
        for v in val:
            h, w = v.shape[0] // 2 * 2, v.shape[1] // 2 * 2
            y = torch.from_numpy(v[:h, :w].astype(np.float32) / 255).to(dev)[None, None]
            so, to = st(y), teacher(y)
            r.append(psnr(so[..., MV:-MV, MV:-MV], to[..., MV:-MV, MV:-MV])); rf.append(psnr(so, to))
        EDGE[:] = [np.mean(rf)]
        y1 = torch.from_numpy(Yof(P["s2-k40_f1"][..., ::-1].copy()).astype(np.float32) / 255).to(dev)[None, None]
        gt = torch.from_numpy(Yof(P[f"s2-k40_f{S}"][..., ::-1].copy()).astype(np.float32) / 255).to(dev)[None, None] if S in (2, 3) else None
        g = (psnr(st(y1)[..., MV:-MV, MV:-MV], gt[..., MV:-MV, MV:-MV]), psnr(teacher(y1)[..., MV:-MV, MV:-MV], gt[..., MV:-MV, MV:-MV])) if gt is not None else (0, 0)
    st.train(); return np.mean(r), g


e, g = evaluate(); print(f"start: to the teacher {e:.2f} dB; to the honest {S}x: student {g[0]:.2f}, teacher {g[1]:.2f}", flush=True)
opt = torch.optim.Adam(st.parameters(), lr=a.lr)
sched = torch.optim.lr_scheduler.OneCycleLR(opt, max_lr=a.lr, total_steps=a.steps, pct_start=0.05)
t0 = time.time(); best = -1; pl = None
for it in range(1, a.steps + 1):
    if (it - 1) % 10000 == 0: pl = pool()
    x = batch(pl)
    with torch.no_grad(): tgt = teacher(x)
    out = st(x, clamp=False)
    m = a.margin
    o, t = (out[..., m:-m, m:-m], tgt[..., m:-m, m:-m]) if m else (out, tgt)
    loss = F.l1_loss(o, t) + 4 * F.mse_loss(o, t)
    opt.zero_grad(); loss.backward(); opt.step(); sched.step()
    if it % 2500 == 0 or it == a.steps:
        e, g = evaluate()
        if EDGE[0] > best: best = EDGE[0]; torch.save(st.state_dict(), f"{OUT}/{tag}.pt")   # the best over the whole frame, edges included
        print(f"  step {it}: loss {loss.item():.5f}, to the teacher {e:.2f} dB (edges included {EDGE[0]:.2f}), "
              f"to the honest {S}x {g[0]:.2f} (teacher {g[1]:.2f}), {time.time() - t0:.0f} s", flush=True)
print(f"best to the teacher (edges included) {best:.2f} dB -> {OUT}/{tag}.pt")
