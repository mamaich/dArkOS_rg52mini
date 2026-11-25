# libnpusr_gles - NPU UPSCALE for GLES emulators (LD_PRELOAD)

x3 super-resolution on the RK3562's NPU for programs that draw with EGL/GLES and show their native frame
at an integer x3 scale, nearest - the counterpart of the Vulkan rotate layer's (`vk-rotate-layer/npusr.inc.c`)
for what goes out past Vulkan. The image uses it in RetroArch for the cores `darkos-npusr` lists in
`GLES_CORES`: PS1 (`pcsx_rearmed` in the 32-bit RetroArch, `duckstation` and `swanstation` in the 64-bit
ones) and, since 2026-11-24, Mega Drive, Sega CD and 32X, SNES, NES, PC Engine and Neo Geo, in whichever of the
three RetroArchs ES starts them (gl, KMS, RGA turning the picture onto the panel; the library built for armhf
and aarch64). The emulator is not changed; `retroarch32`, `retroarch` and `retroarch-next` preload the library
when NPU UPSCALE is on (docs/KNOWN-ISSUES.md, "NPU UPSCALE").
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
- A TV (HDMI, 1920x1080; 2026-11-25): RetroArch's integer scale shows a 224- or 240-line frame x4 there.
  A viewport that is k > S times such a frame (k up to 8, the frame's width even and 160 to 400) is taken
  too: the S times picture is composed into an image of the library's own (made at the first such frame)
  and stretched over the viewport with one bilinear `glBlitFramebuffer`. Mega Drive on the TV: 60 fps,
  Mali 27% (nearest x4: 7%). The panel never sees it (x4 does not fit 720 lines). An x4 model
  (`x4:auto`) works too, but on the TV it gave the same picture at the same Mali, so it is not used.
- `:fit` (`x3:auto:fit`; the owner, 2026-11-25: on a TV the picture should fill the screen, not sit in
  RetroArch's integer x4 with black bars all round): the S times picture is stretched, bilinear, over the
  largest rectangle of the frame's shape in the window, centred - 1440x1080 for 320x240 on 1080p, 1234x1080
  for 256x224 - wherever that is not the S times area itself; RetroArch's bars (cleared each frame) are drawn
  over. It works on any TV size: 1360x768 or 1600x900 (RetroArch's integer x3 there) are stretched from x3,
  a 720p TV is the panel's size (240 lines x3 in place, 224 lines stretched to 720). `darkos-npusr gles`
  prints the spec on its second line, `:fit` with HDMI connected; the launchers pass it on. Mega Drive on
  1080p: 60 fps, Mali 17%; PS1 (pcsx_rearmed): 60 fps, 50% (42% with the bars).
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
Seven sizes, all of one network: since 2026-11-25 `x3_b7_gtr_last.pt`, the AI-SR session's x3 trained on true
high-resolution targets with no teacher (npusr/NOTICE, item 5; the 3DS's and the PSP's x3 are the same network),
made with `npusr/models/s2d_rknn.py` (`--weights x3_b7_gtr_last.pt --scale 3 --blocks 7 --size WxH --calib
calib-mix`, the output renamed from `espcn_x3_<W>x<H>.rknn`) - a rebuild gives the same files (all of them checked
2026-11-25: the `onnx` step with torch 2.11, the `rknn` step with rknn-toolkit2 2.5.0). The calibration frames
(`calib-mix`: frames of the owner's games, among them these systems', and sheets of the training inputs) are not
in the sources, their sha256 are (`npusr/models/gt/calib-mix.SHA256SUMS`). Until 2026-11-25 the models were
`s2d_x3_b7.pt` (distilled from ESPCN) calibrated on 3DS frames only; the new ones gave clearly sharper edges and
text (by eye on 16-bit, PS1, N64 and Saturn frames) and +0.4 dB on Super Mario 3D Land against an honest 3x.
The 256, 320 and 224/240 sizes are also the Mega Drive's, SNES's,
NES's and PC Engine's frames; 304x224, added 2026-11-24, is Neo Geo's visible area (FinalBurn Neo).
PS1 widths are 256, 320, 368, 512 and 640; only up to 426 x 240 fits x3 on 1280x720, so 512 and 640 wide
and PAL's 256 or 288 lines get no model. The 224-line ones are for NTSC games with a shorter range and for
DuckStation's default overscan crop. RetroArch shows a frame at exactly x3 only when the core's aspect is the
frame's own: DuckStation and SwanStation get `PAR 1:1` for the run, the other cores (pcsx_rearmed and the 16-bit
consoles', which report 4:3 whatever the frame) RetroArch's square pixel (docs/KNOWN-ISSUES.md, "Which frames
get exactly x3"). The column on the right: games of that frame size NPU UPSCALE ran in on the console (with the
models before 2026-11-25; the new ones ran 256x224, 320x224 and 320x240 in a game on the TV, and all seven on
the NPU against the float network, 43.6-46.0 dB).

| file | sha256 | on the console |
|---|---|---|
| x3_256x240.rknn | 07f48bf9ad625106aeb5faec2ffda3cde882134e54e5a7b953de8d870c1dc032 | P-47 (PC Engine) |
| x3_320x240.rknn | 7450a6987c17279f3f9317af391c4a817a84071bcd1629f64a2fc81e19d6b669 | Double Dragon, Hover Racing (pcsx_rearmed); Double Dragon (SwanStation) |
| x3_368x240.rknn | d8086c704fdc101acb6ce414ba19be1557eeb6a4d946d787cb57dce620e43cf4 | |
| x3_256x224.rknn | cd8f2573a8e556ae7e856ca28f6a647716e529566f15709401973fc39e508aa7 | the BIOS logo (DuckStation); Zombies Ate My Neighbors (Mega Drive), Real Monsters (SNES), The Addams Family (NES) |
| x3_320x224.rknn | 2fb7e9e25c2cdb570ce20dbcbc9b87555f517eaf734aa68671c412c2da02094f | Double Dragon, Hover Racing (DuckStation); Blackthorne (32X) |
| x3_368x224.rknn | 60eda41ba888b81d7ad74cd8b67d32fcf40266ce8228d0efab6581a1738aee4b | |
| x3_304x224.rknn | c5beaac45a77a499518afa6a7ae0d07cf19320b44acf124647de4c03238cbcbf | Kizuna Encounter (Neo Geo, FinalBurn Neo) |

## Measured (retroarch32 + pcsx_rearmed, NPU at 600 MHz, the AI-SR session)
Double Dragon 60 fps, Mali 3.8 -> 23.5-24%; Hover Racing (PAL) 50 fps, Mali 4.9 -> 23.5-24.8%; the NPU
6.0-6.5 ms a frame, the hook 1.5-1.8 ms of CPU a frame. Against pcsx_rearmed's own enhanced resolution: that
one sharpens only the 3D geometry (textures and dithering stay) and costs the GPU thread on the CPU (7 -> 25%),
and does nothing for 2D games; the upscale smooths textures and dithering too.
In the 64-bit RetroArch (2026-11-24, this session): Double Dragon on DuckStation (320x224) 60 fps, the NPU
4.8-5.8 ms, the hook 1.0-1.3 ms, GPU 3 -> 21-24%; on SwanStation (320x240) 60 fps, 6.4-6.5 ms, GPU 21%.
In `retroarch-next` the same: DuckStation 60 fps, 4.9-5.2 ms, GPU 4 -> 19%; SwanStation 60 fps, 6.5 ms.
The 16-bit consoles (2026-11-24): 60 fps (the SNES game 50, a PAL release) at 4.9-6.0 ms of NPU, GPU 2-4 ->
14-20%; the table in docs/KNOWN-ISSUES.md, "The 16-bit consoles".
