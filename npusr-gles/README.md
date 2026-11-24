# libnpusr_gles - NPU UPSCALE for GLES emulators (LD_PRELOAD)

x3 super-resolution on the RK3562's NPU for programs that draw with EGL/GLES and show their native frame
at an integer x3 scale, nearest - the counterpart of the Vulkan rotate layer's (`vk-rotate-layer/npusr.inc.c`)
for what goes out past Vulkan. The image uses it for PS1 only: `pcsx_rearmed` and `pcsx_rearmed_rumble` in
the 32-bit RetroArch, `duckstation` and `swanstation` in the 64-bit one (gl, KMS, RGA turning the picture onto
the panel; the library built for each). The emulator is not changed; `retroarch32` and `retroarch` preload the
library when NPU UPSCALE is on (docs/KNOWN-ISSUES.md, "NPU UPSCALE").
Written by the AI-SR session (2026-11-24); `npusr_gles.c` is byte for byte the source of the build tested on
the console (sha256 7e1a3c04d47a359110a36c770be1c507226a8d08b0f89da10d353d391620fd58).

## Build
`build_npusr.sh` runs `build.sh` twice. In the armhf build chroot (Arkbuild32) with its gcc, the EGL, GLES
and KHR headers come from libegl-dev, libgles-dev and libgl-dev unpacked into `sys/` (not installed: libglvnd's
libEGL would replace the chroot's links to libmali); checked 2026-11-24 in a trixie armhf chroot with no GL
headers of its own. In the aarch64 one (Arkbuild) those three packages are installed with the deps; checked the
same day in a copy of Arkbuild after the deps (gcc 12.4). `build.sh` picks the CPU flags by the compiler's
target; `rknn_api.h` (rknn-toolkit2 2.5.0) is copied in from `vk-rotate-layer/`. Nothing but libc, libdl,
libpthread and libm is linked: the program's EGL/GLES are found through `dlsym(RTLD_NEXT)` and `eglGetProcAddress`,
librknnrt with `dlopen(DARKOS_NPU_SR_LIB)`. Exported: `eglSwapBuffers`, `eglGetProcAddress`, `eglTerminate`,
`glViewport`, `SDL_GL_SwapWindow`, `SDL_Quit` (the SDL ones for SDL programs; retroarch32 calls neither).

## How it works
- Before the program's real swap, in a GLES 3.1 context of its own on the program's window (its GL state
  untouched): two `glBlitFramebuffer` take a block's centre and its right neighbour from each 3x3 block of the
  native frame; a pass renders the NPU's input (NPU memory as a dma-buf imported as an RGBA8 EGLImage: 4 int8
  channels = a 2x2 space-to-depth cell) and a compute pass counts the blocks not one colour; an EGL fence.
- A worker thread waits for the fence and runs the NPU; three slots for each size, as in the layer: the
  program never waits for the NPU, the picture can be a frame behind.
- The compose: the NPU's finished output (NC1HWC2 as an RGBA8 texture) and the colour of the same slot drawn
  into the back buffer (`DARKOS_NPU_SR_COMPOSE=swap`, the default; `async` composes on the worker's own
  surfaceless context and only copies on the swap - for heavy emulators, +6 points of Mali). With more than
  1% of the blocks not one colour (a menu, text over the frame, a frame not at its native size) the frame is
  shown as the program drew it.

## Runtime
```
LD_PRELOAD=/usr/lib/arm-linux-gnueabihf/libnpusr_gles.so
DARKOS_NPU_SR=x3:auto
DARKOS_NPU_SR_MODEL=/usr/share/darkos/npusr/x3_{W}x{H}.rknn
DARKOS_NPU_SR_LIB=/usr/lib/arm-linux-gnueabihf/librknnrt.so
```
- `x3:auto`: the native size from `glViewport` - a viewport of exactly 3W x 3H gives W x H, and the model
  path with `{W}`, `{H}` (and `{S}` = 3) replaced. Up to 6 sizes get an engine per process.
- No model for a size (an interlaced 640x480 or 512x240 mode, enhanced resolution left on): one line
  `darkos_npusr_gles: WxH: no model <path>`, that size is not tried again, its frames shown untouched
  (~0.3 ms of CPU a frame). No NPU (no rknpu node, no librknnrt, init failed): `NPU SR unavailable, off`,
  and everything passes through. rknpu must be loaded before the game, as `darkos-npusr` does.
- Known limit: RetroArch sets a later viewport of its own (640x480 at (320,120)), so the last x3 viewport is
  kept as the frame's area. In a hi-res interlaced mode the NPU keeps working on that area and the result is
  thrown away as "menu": the picture is right, at ~5 points of Mali, while such a mode lasts. Not to be
  fixed (the owner, 2026-11-24): for a game that runs in such a mode, NPU UPSCALE is turned off.
- Exit: `eglTerminate`, `SDL_Quit`, an `atexit` handler registered after librknnrt is loaded (so it runs
  before librknnrt's destructors) and a SIGTERM/SIGINT/SIGHUP/SIGQUIT handler (only where the program has
  none) stop the worker, wait for the job on the NPU (up to 1 s in the signal handler) and free its memory:
  a program leaving with a job on the NPU crashed rknpu 0.9.8 on 2026-11-24 (fixed in kernel_rk3562 too).
- `DARKOS_NPU_SR_STATS=1`: a line every 5 s. Test knobs, inert unless set: `_EXP`, `_ALLOC`, `_HEAP`,
  `_DUMP`, `_DUMP_AT`.

## Models (`npusr/models/x3_<W>x<H>.rknn`)
Six sizes, all of one network: `s2d_x3_b7.pt`, the 3DS's (npusr/NOTICE), made with `npusr/models/s2d_rknn.py`
(`--weights s2d_x3_b7.pt --scale 3 --blocks 7 --size WxH --calib <the 3DS's calibration frames>`: the same
14 frames of Super Mario 3D Land, each resized to W x H; no PS1 frames) - a rebuild gives the same files.
PS1 widths are 256, 320, 368, 512 and 640; only up to 426 x 240 fits x3 on 1280x720, so 512 and 640 wide
and PAL's 256 or 288 lines get no model. The 224-line ones are for NTSC games with a shorter range and for
DuckStation's default overscan crop. RetroArch shows a frame at exactly x3 only when the core's aspect is the
frame's own: DuckStation and SwanStation get `PAR 1:1` for the run, pcsx_rearmed reports 4:3 always, so of its
frames only 320x240 is enlarged (docs/KNOWN-ISSUES.md, "Which frames get exactly x3").

| file | sha256 | on the console |
|---|---|---|
| x3_256x240.rknn | a9b30e5689d512c309b4efc67bb07d7bf2a896c463e4ad6bff1a93c3dce1d82c | |
| x3_320x240.rknn | 88fdb208a19f889fe570378786d0b450f460ae157f893ed449fb0366c4a38fed | Double Dragon, Hover Racing (pcsx_rearmed); Double Dragon (SwanStation) |
| x3_368x240.rknn | 6376aad8403a2961683b1fc9cfaef158fabfbed8aa92f1a30b6ab1c69ca9864d | |
| x3_256x224.rknn | 0b926d27b893cc6fd2b181d805618c4d11774a9705a1c6bd7f1a6f6871284e89 | the BIOS logo (DuckStation) |
| x3_320x224.rknn | a442be0b167242cd448dcc56eb9ae6a52e4072d7cd1b2aad23cc1fd066a50158 | Double Dragon, Hover Racing (DuckStation) |
| x3_368x224.rknn | e5788ed026513d42e8573409b0609c719a58d952618b682383ad5ca7c712a2ba | |

## Measured (retroarch32 + pcsx_rearmed, NPU at 600 MHz, the AI-SR session)
Double Dragon 60 fps, Mali 3.8 -> 23.5-24%; Hover Racing (PAL) 50 fps, Mali 4.9 -> 23.5-24.8%; the NPU
6.0-6.5 ms a frame, the hook 1.5-1.8 ms of CPU a frame. Against pcsx_rearmed's own enhanced resolution: that
one sharpens only the 3D geometry (textures and dithering stay) and costs the GPU thread on the CPU (7 -> 25%),
and does nothing for 2D games; the upscale smooths textures and dithering too.
In the 64-bit RetroArch (2026-11-24, this session): Double Dragon on DuckStation (320x224) 60 fps, the NPU
4.8-5.8 ms, the hook 1.0-1.3 ms, GPU 3 -> 21-24%; on SwanStation (320x240) 60 fps, 6.4-6.5 ms, GPU 21%.
