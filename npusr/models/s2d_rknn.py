#!/usr/bin/env python3
"""A space-to-depth model's weights (s2d_distill.py, gt/s2d_gt.py) -> ONNX -> RKNN INT8 for the RK3562, for NPU UPSCALE.

The network: the input is space-to-depth 2x2 of the luma, [1, 4, H/2, W/2] (channel dy*2+dx, 0..255, std 255);
conv 3x3 4->C, NB x (conv 3x3 C->C, ReLU), conv 3x3 C->(2S)^2, Clip [0,1]. The output is [1, (2S)^2, H/2, W/2]
before the pixel shuffle (channel dy*2S+dx); the layer's shaders shuffle it and add the colour.

usage, in two steps (as the PSP's model was made: torch 2.x for the export, rknn-toolkit2 2.5.0 for the rest):
  python s2d_rknn.py onnx --weights s2d_x2_v7.pt --scale 2 --size 480x272 --calib calib-psp [--work out]
  python s2d_rknn.py rknn --weights s2d_x2_v7.pt --scale 2 --size 480x272 --calib calib-psp [--work out]
  --calib  a directory of the system's frames (png, colour or Y already); other sizes are resized to WxH
           (0-2 pixels bigger: cropped). The x3 and x4 of 2026-11-25: --weights x3_b7_gtr_last.pt --blocks 7, or
           x4_b9_gtr_last.pt --scale 4 --blocks 9, --calib calib-mix (gt/calib-mix.SHA256SUMS; not in the sources);
           the 16-bit and PS1 sizes are installed as x3_<W>x<H>.rknn.
  Makes <work>/espcn_x<S>_<W>x<H>.rknn (the name the layer looks for in /usr/share/darkos/npusr/) and prints its
  sha256. The calibration frames (screenshots of the system's games) are not in the sources; the same weights and
  frames give the same model, byte for byte."""
import argparse, glob, hashlib, os
import numpy as np, cv2

ap = argparse.ArgumentParser()
ap.add_argument("stage", choices=("onnx", "rknn"))
ap.add_argument("--weights", required=True, help="a .pt state dict from s2d_distill.py")
ap.add_argument("--scale", type=int, required=True)
ap.add_argument("--ch", type=int, default=32)
ap.add_argument("--blocks", type=int, default=5)
ap.add_argument("--size", required=True, help="the native frame WxH, e.g. 480x272")
ap.add_argument("--calib", required=True)
ap.add_argument("--work", default="out")
a = ap.parse_args()
S, C, NB, R = a.scale, a.ch, a.blocks, 2 * a.scale
W, H = map(int, a.size.lower().split("x"))
D = os.path.abspath(a.work); os.makedirs(D, exist_ok=True)
name = f"{os.path.splitext(os.path.basename(a.weights))[0]}_{W}x{H}"
Yof = lambda im: cv2.cvtColor(im, cv2.COLOR_BGR2YCrCb)[..., 0] if im.ndim == 3 else im


def frames():
    out = []
    for f in sorted(glob.glob(os.path.join(a.calib, "*.png"))):
        y = Yof(cv2.imread(f, cv2.IMREAD_UNCHANGED))
        dh, dw = y.shape[0] - H, y.shape[1] - W
        if 0 <= dh <= 2 and 0 <= dw <= 2:      # 480x272 -> 480x270 (as PPSSPP DisplayCropTo16x9), 400 -> 399: crop, not resize
            y = y[dh // 2:dh // 2 + H, dw // 2:dw // 2 + W]
        if y.shape != (H, W): y = cv2.resize(y, (W, H), interpolation=cv2.INTER_AREA if y.shape[0] > H else cv2.INTER_CUBIC)
        out.append(y)
    return out


s2d = lambda y: y.reshape(H // 2, 2, W // 2, 2).transpose(1, 3, 0, 2).reshape(4, H // 2, W // 2)   # channel dy*2+dx

if a.stage == "onnx":
    import torch, torch.nn as nn, torch.nn.functional as F

    class Core(nn.Module):
        def __init__(self):
            super().__init__()
            self.c = nn.ModuleList([nn.Conv2d(4, C, 3, padding=1)] + [nn.Conv2d(C, C, 3, padding=1) for _ in range(NB)] + [nn.Conv2d(C, R * R, 3, padding=1)])

        def forward(self, x):
            x = self.c[0](x)
            for k in range(1, NB + 1): x = F.relu(self.c[k](x))
            return self.c[-1](x).clamp(0, 1)

    m = Core().eval(); m.load_state_dict(torch.load(a.weights, map_location="cpu"))
    torch.onnx.export(m, torch.zeros(1, 4, H // 2, W // 2), f"{D}/{name}.onnx", opset_version=12, input_names=["x"], output_names=["y"], dynamo=False)
    cal = []
    os.makedirs(f"{D}/calib-{name}", exist_ok=True)
    for k, y in enumerate(frames()):
        p = f"{D}/calib-{name}/{k:03d}.npy"; np.save(p, s2d(y.astype(np.float32))[None]); cal.append(p)
        with torch.no_grad():   # the float model's output, to compare the INT8 one with
            np.save(f"{D}/calib-{name}/{k:03d}.float.npy", m(torch.from_numpy(s2d(y.astype(np.float32) / 255)[None])).numpy())
    open(f"{D}/calib-{name}/dataset.txt", "w").write("\n".join(cal) + "\n")
    print("onnx ok", name, len(cal))
else:
    from rknn.api import RKNN
    rk = RKNN(verbose=False)
    rk.config(mean_values=[[0] * 4], std_values=[[255] * 4], target_platform="rk3562", quantized_dtype="w8a8", optimization_level=3)
    assert rk.load_onnx(f"{D}/{name}.onnx") == 0
    assert rk.build(do_quantization=True, dataset=f"{D}/calib-{name}/dataset.txt") == 0
    out = f"{D}/espcn_x{S}_{W}x{H}.rknn"
    assert rk.export_rknn(out) == 0
    assert rk.init_runtime() == 0   # the toolkit's simulator
    res = []
    fl = sorted(glob.glob(f"{D}/calib-{name}/*.float.npy"))
    for k in range(0, len(fl), max(1, len(fl) // 3))[:3]:
        x = np.load(f"{D}/calib-{name}/{k:03d}.npy")[0].transpose(1, 2, 0).astype(np.uint8)[None]
        o = rk.inference(inputs=[x], data_format=["nhwc"])[0]
        f = np.load(fl[k])
        res.append(10 * np.log10(1 / np.mean((np.clip(o, 0, 1) - f) ** 2)))
    rk.release()
    print(f"{name}: INT8 (simulator) against float: {' / '.join(f'{r:.1f}' for r in res)} dB")
    print(f"{out}\nsha256 {hashlib.sha256(open(out, 'rb').read()).hexdigest()}")
