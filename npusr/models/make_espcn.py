#!/usr/bin/env python3
"""ESPCN xS (OpenCV dnn_superres, github.com/fannymonori/TF-ESPCN, Apache-2.0) -> RKNN INT8 for the RK3562, for NPU UPSCALE.

The first layer rebuilt exactly ("row5": the same network to the bit, 2.5 times faster on the NPU):
  the input is 5 channels = the frame's Y shifted vertically by dy = -2..2 (zero past the edge; channel k = row y+k-2),
  conv1 1x5 5->64 (the rows of the 5x5 kernel), ReLU, conv2 3x3 64->32, ReLU, conv3 3x3 32->S*S, tanh
  (in the .pb Tanh comes after DepthToSpace; it is elementwise, so it moved before the shuffle).
The output is [1, S*S, H, W] before the pixel shuffle: channel c = dy*S + dx (the layer's shader shuffles and adds colour).
Input Y = 0.299R + 0.587G + 0.114B (BT.601, full range), uint8; mean 0, std 255.

usage:  python make_espcn.py --scale 3 --size 400x240 --calib calib-3ds [--out out]
  --calib  a directory of the system's frames (png/ppm/jpg, colour or Y already); other sizes are resized to WxH.
  Makes <out>/espcn_x<S>_<W>x<H>.rknn (the name the layer looks for in /usr/share/darkos/npusr/) and prints its sha256.
Needs rknn-toolkit2 2.5.0, torch, onnx and opencv-python (x86-64 Linux). The calibration frames (screenshots of
the system's games) are not in the sources; the same frames give the same model, byte for byte."""
import argparse, glob, hashlib, os, sys
import numpy as np, cv2, torch, torch.nn as nn, torch.nn.functional as F

ap = argparse.ArgumentParser()
ap.add_argument("--scale", type=int, required=True, choices=(2, 3, 4))
ap.add_argument("--size", required=True, help="the native frame WxH, e.g. 400x240")
ap.add_argument("--calib", required=True)
ap.add_argument("--out", default="out")
ap.add_argument("--pb", default=None, help="default: ESPCN_x<S>.pb beside this script")
a = ap.parse_args()
S = a.scale; W, H = map(int, a.size.lower().split("x"))
here = os.path.dirname(os.path.abspath(__file__))
pb = a.pb or os.path.join(here, f"ESPCN_x{S}.pb")
a.out = os.path.abspath(a.out); os.makedirs(a.out, exist_ok=True)
name = f"espcn_x{S}_{W}x{H}"

net = cv2.dnn.readNetFromTensorflow(pb)
gp = lambda n: torch.from_numpy(net.getParam(net.getLayerId(n), 0).copy())
W1, B1, W2, B2, W3, B3 = gp("conv1"), gp("add").reshape(-1), gp("conv2"), gp("add_1").reshape(-1), gp("conv3"), gp("add_2").reshape(-1)
assert W3.shape[0] == S * S, f"{pb}: conv3 gives {W3.shape[0]} channels, not {S * S}"


class Orig(nn.Module):        # the network as it is (5x5), to compare with
    def forward(self, y):
        x = F.relu(F.conv2d(y, W1, B1, padding=2)); x = F.relu(F.conv2d(x, W2, B2, padding=1))
        return torch.tanh(F.conv2d(x, W3, B3, padding=1))


class Row5(nn.Module):
    def __init__(self):
        super().__init__()
        self.c1 = nn.Conv2d(5, 64, (1, 5), padding=(0, 2)); self.c2 = nn.Conv2d(64, 32, 3, padding=1); self.c3 = nn.Conv2d(32, S * S, 3, padding=1)
        with torch.no_grad():
            self.c1.weight.copy_(W1[:, 0][:, :, None, :]); self.c1.bias.copy_(B1)
            self.c2.weight.copy_(W2); self.c2.bias.copy_(B2); self.c3.weight.copy_(W3); self.c3.bias.copy_(B3)

    def forward(self, x):
        return torch.tanh(self.c3(F.relu(self.c2(F.relu(self.c1(x))))))


def row5(y):                  # y: N,1,H,W -> N,5,H,W
    p = F.pad(y, (0, 0, 2, 2))
    return torch.cat([p[:, :, k:k + y.shape[2]] for k in range(5)], 1)


def load_y(f):                # a frame -> Y uint8 HxW of the size wanted
    im = cv2.imread(f, cv2.IMREAD_UNCHANGED)
    if im is None: return None
    if im.ndim == 3: im = cv2.cvtColor(im[..., :3], cv2.COLOR_BGR2YCrCb)[..., 0]
    if im.shape != (H, W):
        im = cv2.resize(im, (W, H), interpolation=cv2.INTER_AREA if im.shape[0] > H else cv2.INTER_CUBIC)
    return im


files = sorted(f for f in glob.glob(os.path.join(a.calib, "*")) if f.lower().endswith((".png", ".ppm", ".jpg", ".jpeg", ".bmp")))
ys = [y for y in map(load_y, files) if y is not None]
assert ys, f"no frames in {a.calib}"
m0, m = Orig().eval(), Row5().eval()
with torch.no_grad():
    t = torch.from_numpy(ys[0].astype(np.float32) / 255)[None, None]
    d = (m(row5(t)) - m0(t)).abs().max().item()
print(f"{name}: row5 against the 5x5 one: max |diff| {d:.2e}")
if hasattr(cv2, "dnn_superres"):   # against OpenCV (needs opencv-contrib)
    sr = cv2.dnn_superres.DnnSuperResImpl_create(); sr.readModel(pb); sr.setModel("espcn", S)
    ref = sr.upsample(cv2.merge([ys[0]] * 3))[..., 0].astype(np.float32)
    with torch.no_grad():
        mine = F.pixel_shuffle(m0(t), S)[0, 0].numpy() * 255
    print(f"   against OpenCV dnn_superres: max |diff| {np.abs(np.clip(mine, 0, 255) - ref).max():.1f} (of 255)")

onnx_p = os.path.join(a.out, name + ".onnx")
kw = dict(opset_version=12, input_names=["x"], output_names=["y"])
try:
    torch.onnx.export(m, torch.zeros(1, 5, H, W), onnx_p, dynamo=False, **kw)
except TypeError:
    torch.onnx.export(m, torch.zeros(1, 5, H, W), onnx_p, **kw)
cd = os.path.join(a.out, f"calib-{name}"); os.makedirs(cd, exist_ok=True)
cal = []
for k, y in enumerate(ys):
    p = os.path.join(cd, f"{k:03d}.npy")
    np.save(p, row5(torch.from_numpy(y.astype(np.float32))[None, None]).numpy()); cal.append(p)
open(os.path.join(cd, "dataset.txt"), "w").write("\n".join(cal) + "\n")

from rknn.api import RKNN
rk = RKNN(verbose=False)
rk.config(mean_values=[[0] * 5], std_values=[[255] * 5], target_platform="rk3562", quantized_dtype="w8a8", optimization_level=3)
assert rk.load_onnx(onnx_p) == 0
assert rk.build(do_quantization=True, dataset=os.path.join(cd, "dataset.txt")) == 0
out = os.path.join(a.out, name + ".rknn")
assert rk.export_rknn(out) == 0
assert rk.init_runtime() == 0          # the simulator: INT8 against float
res = []
for y in ys[::max(1, len(ys) // 3)][:3]:   # frames from all over the set
    x = row5(torch.from_numpy(y.astype(np.float32))[None, None])[0].permute(1, 2, 0).numpy().astype(np.uint8)[None]
    o = rk.inference(inputs=[x], data_format=["nhwc"])[0]
    with torch.no_grad():
        f = m(row5(torch.from_numpy(y.astype(np.float32) / 255)[None, None])).numpy()
    res.append(10 * np.log10(1 / np.mean((np.clip(o, 0, 1) - np.clip(f, 0, 1)) ** 2)))
rk.release()
print(f"{name}: calibration {len(ys)} frames; INT8 (simulator) against float: {' / '.join(f'{r:.1f}' for r in res)} dB")
print(f"{out}\nsha256 {hashlib.sha256(open(out, 'rb').read()).hexdigest()}")
