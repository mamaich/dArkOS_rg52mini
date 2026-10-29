# What is next

A working list, not a roadmap. Ordered by what a build would answer soonest.

## Shipped in v09162026

All of it is on the device and released, so it is history rather than a list to
work through: the boot logo from both halves, `Playback Path = SPK` by default,
zram on lzo-rle with the eMMC partition as a second tier, stripped modules,
Cortex-A53 flags for everything that rebuilt, Kodi, Yabasanshiro, freej2me-plus,
our own U-Boot, and the component audit that runs at the end of every build.

The kernel changes that landed after the build finished — the RK628 bridge
fix, the hang detectors, the bootloader log — were rebuilt and written into the
released image by hand rather than left waiting.

## Shipped in v09292026

Everything the previous list was waiting on, plus what was found while building
it. Confirmed on the device.

* **flycast and fake-08 are optimised now** — they had been built at `-O0` and
  `-g`. PPSSPP's `flags.make` was read out of the running build to prove the
  flags arrive, and the cache key change meant it rebuilt from source rather
  than restoring the artifact made under the old flags.
* **`perfmax` sets the GPU governor**, which it never did on this SoC — it was
  writing to the RK3566 node, so every game ran on `simple_ondemand`.
* **BlueZ accepts HID from unbonded devices**, which is most legacy Bluetooth
  gamepads. It was in the released image by hand; now it is in the build.
* **The Bluetooth driver no longer frees every failed packet twice.**
  `bt_sdio_recv()` in aic8800 called `kfree_skb()` after `hci_recv_frame()`
  had already freed it — 150 to 200 double frees per shutdown with Bluetooth
  on, a corrupted slab, and a panic somewhere unrelated a moment later. See
  KNOWN-ISSUES.
* **The two GPU drivers that cannot bind here are gone**, and so is the
  debugging the device never reads: `Image` is 6.0 MB smaller. See
  PERFORMANCE and HARDWARE.
* **OP-TEE is off on both sides** — driver and bootloader — which returns the
  2 MiB SHM window. See HARDWARE. (v10052026 went back to the stock BL32
  v1.07, whose region is 2 MiB larger.)
* **RetroArch gets its cores even when GitHub says 429.** 269 cores
  downloaded, 8 of them only after a retry; plain `wget -t` counts network
  failures and treats an HTTP error as a final answer, so those 8 used to go
  missing silently.
* **A workaround for the revision A black screen**, added to the assets on
  2026-10-03: `panel-kick` re-initialises the display before EmulationStation,
  letting the DSI host and its PHY runtime-suspend first. The mechanism was
  found on revision B; confirmed working on a revision A unit by its owner.
  KNOWN-ISSUES has the evidence.

## Upstream updates for 1.4 (2026-10-28)

`tools/upstream/check.py` on 2026-10-27 found newer releases of ten components
and twelve commits in christianhaitian's recipes. Each was read (what changed,
whether our patches apply, what the recipe needs), built in a chroot of the
cached rootfs as the image build does, and tried on the console against
1.3.1's build, with the new files bound over the image's for the run.

| Component | Taken | On the console (fps, against 1.3.1) |
|---|---|---|
| Azahar | 2126.1.2 -> 2126.2, GammaOS's 1feeb9c2d16 as patch 019 | Super Mario 3D Land 58.8 / 59.1 (seven runs each, within the spread), Castlevania 21.5 / 21.5; 1.3 states and pipeline caches load |
| ARMSX2 | stays at a7ad453 (2.9 tried, the owner's call of 2026-10-29: speed comes before keeping up with upstream) | 2.9 with its profile retrained: Jak's intro 34.1 / 34.2, God of War on the ship 20.7 / 21.1 |
| Dolphin | e6583f8 (2023) -> 2609a | ND Demo 45.7 / 45.6, 46.3 with AccurateFmadds and PrecisionFrameTiming off (the image's setting); Metroid Prime starts, Hotkey+Start quits |
| flycast | v2.6 -> v2.7 and four fixes from master | Crazy Taxi 59.9; kov7sprt keeps its NVRAM |
| ScummVM | 2026.3.0 -> 2026.4.0 | Day of the Tentacle, now with FluidSynth |
| openMSX | 17.0 -> 19.1 | C-BIOS (no MSX game on the card) |
| XRoar | 1.11 -> 1.12.1 | the Dragon 32's BASIC (no game on the card) |
| gl4es | 2037865 -> 1891af3 | not run: Don't Starve brings its own |
| retrorun | pinned at 0784f21, what 1.3.1 ships | — |
| christianhaitian's recipes | merged to ef273c8 | of the image only ScummVM |

* **ARMSX2 2.9, tried and not taken** (dce7905, reverted 2026-10-29). It
  brings fixes in the GS hardware renderer's texture cache and Vulkan render
  passes, none of them seen in our games, and was no faster even with its own
  profile (above), so the owner kept a7ad453 and its 2026-10-21 profile. A
  newer ARMSX2 is taken only if it is faster here. What was learnt with it:
* **ARMSX2's profile.** With a7ad453's profile 2.9 was 3% slower on Jak's
  intro and 4% on God of War: the GS code 2.9 changed (GSRendererHW::Draw,
  the texture cache) had no profile data, and the Vulkan changes were not it
  (2.9 without them measured the same). An instrumented 2.9 was run on the
  console (Jak from boot to its title, Jak and God of War from the owner's
  savestates, God of War on the ship; one Jak run gave an "invalid profile",
  probably counters racing between threads, and was run again) and merged
  with the old profile, each about the same total count: clang takes for a
  function the record that fits its code, so unchanged functions keep the
  owner's play and changed ones get 2.9's. The God of War savestate of the
  owner stops in its pause menu; the run presses Cross 15 s in. The amd64
  clang of xtool has no aarch64 profile runtime: an instrumented build
  passes `-resource-dir=/usr/lib/llvm-19/lib/clang/19`, the chroot's.
* **ARMSX2's AutoTune** is off by default in 2.9 and was on in 1.3 (our
  template had no key): Jak's intro 32.2 without it, 33.2 with it. With 2.9
  the template said AutoTune = true and the launcher added the key once; a
  later ARMSX2 needs that again.
* **Dolphin 2609a** turned AccurateFmadds and PrecisionFrameTiming on; off
  again in our Dolphin.ini and, once, in a user's (dolphin.sh).
* **Not tried yet:** Dolphin on malisx2 (it starts there since 1.3.1),
  flycast on its other renderers, gl4es 1891af3 with a port that uses the
  system's, openMSX and XRoar with real games.

Left, and why:

* **DuckStation** (0.1-10495 -> 12083): upstream removed the frontend that
  runs without a display server (the "mini" one we ship) in March 2026; 12083
  is Qt only and needs Wayland or X. Its licence (CC BY-NC-ND 4.0, README:
  pre-configured settings and packages count as modifications): since 1.4
  the image carries no settings in DuckStation's folder, the launcher makes
  them on the first start (KNOWN-ISSUES, DuckStation), and the owner wrote to
  its author.
* **UZDoom** (GZDoom's continuation, 4.14.3 / 5.0.3; GZDoom has had no
  release since g4.14.2): a migration, not an update (binary, config and save
  paths renamed; 5.x reworks controller input and dynamic lights). The owner
  decided on 2026-10-28: not until users ask for it, or the work may be for
  nothing; the upstream check lists it as kept on purpose.
* **openMSX 20 and later**: OpenGL 2.1 only (gl4es), an ImGui GUI, scale
  factor 2 at least. A project of its own.
* **Hypseus-Singe 3.x**: SDL3 with SDL3_ttf, _image and _mixer; the sdl2 line
  stopped at v2.12.1, which we have.
* **OpenBOR**: the 2022 pin is deliberate (christianhaitian went back to it
  for 3.x paks); master dropped its Makefile.
* **Azahar 2127.0** (alpha: new save state fields, every shader cache
  rebuilt) and azahar-nano's e62a0042457 (the right-eye disabler by
  framebuffer), which as it is would switch off our stereo_skip (patches
  013/014): it needs adapting and a check on Castlevania.
* **ARMSX2 after 2.9** (we stay at a7ad453, above): the VU program cache rewritten as one pack per game
  in RAM (a day old, up to 128 MB here) and a COP2 clamp change in the JIT.
  master e3c7d21 tried once with 2.9's profile (2026-10-28): Jak's intro 30.2
  fps against 33.9, God of War on the ship 21.6 against 20.7; the first run
  may have been rebuilding its new cache. Measure it again before taking it.
* **The PlayStation emulators compared** (2026-10-28, as ES starts them, 15 s
  after the warm-up, CPU of the whole system): Hover Racing, Double Dragon and
  Final Fantasy IX run at full speed on all four. CPU: RetroArch32
  pcsx_rearmed (ES's default) 9 / 6 / 7%, standalone DuckStation 6 / 7 / 10%,
  the RetroArch duckstation and swanstation cores 15-17 / 10-11 / 17%. So
  standalone DuckStation brings no speed the image would miss without it.
  Side note: pcsx_rearmed shows Final Fantasy IX (the RGR Russian disc,
  SLUS-01251) at 50 fps with the region at auto, the others at 60.
* **GZDoom on the RG52**: checked with Freedoom, and fixed for LZDoom too
  (KNOWN-ISSUES, "Doom: the player turned by himself").
* **Dolphin after 2609a**: #14388 and #14853 (JIT) apply with offsets; worth
  a measurement.

## Check on the device what the upstream sync of 2026-10-05 brought

The sync with christianhaitian/dArkOS and rk3566_core_builds (see the commits
of that date) was done by reading, not by running. Each of these needs a look
on the device:

1. **DSperate** — checked on the device on 2026-10-07 (v10062026, revision B),
   with the Pokemon Diamond & Pearl and Dragon Quest Monsters Joker 2 demos and
   the BIOS and firmware dumps in /roms/bios:
   * **Rotation: fine.** It does not take the KMS page-flip path on this panel
     ("drm: display 0 has no 1280x720 mode (panel is 720x1280)"); it presents
     through an SDL window surface on KMSDRM, so our SDL rotation patch turns
     the picture. Both DS screens stacked, centred, upright.
   * **Input: fine.** nds.sh's RG52 mapping is picked up; DSperate notices that
     SDL's a/b and x/y are swapped against the kernel's button positions and
     corrects it itself. Start+Select quits.
   * **Sound** through ALSA at 48 kHz; one underrun at start.
   * **Speed is the CPU governor, not the build.** Pokemon DP demo, first
     field, walking; clocks sampled twice a second for 10 s:

     | | ondemand (CPU) / simple_ondemand (GPU) / dmc_ondemand | performance |
     |---|---|---|
     | fps | 41-47, audio underruns | 60, none |
     | CPU | 816-2016 MHz, under 1.5 GHz a third of the time | 2016 MHz |
     | GPU | 300 MHz | 900 MHz |
     | DMC | 928 MHz | 928 MHz |

     DSperate keeps about one core busy, which ondemand reads as light load.
     The 3D field of the Joker 2 demo behaves the same way: 48 fps under
     ondemand, 59-60 under performance. Nothing in the image changes this
     (decided 2026-10-07); KNOWN-ISSUES tells users to pick the performance
     governor for NDS in ES. (Superseded in 1.3: perfmax puts `performance`
     first when no governor was chosen, KNOWN-ISSUES.)
   * **Vulkan, in the image since 2026-10-07.** The first 10062026 build had
     DSperate without Vulkan ("gpu3d: unavailable (built without Vulkan)"):
     the presenter and GPU 3D raster need `vulkan/vulkan.h` at build time and
     the chroot had no libvulkan-dev. build_dsperate.sh now installs it (cache
     key suffix `_vk`), the v10062026 image carries that build, and
     dsperate.ini.rk3562 sets `gpu3d = true`. On the device it logs
     "gpu3d: on". In the two demos it made no measurable difference to the
     frame rate - the governor is what matters there - but it moves 3D off the
     CPU for heavier games. Existing cards keep their /roms/nds/dsperate/
     dsperate.ini; nds.sh copies the new default only where there is none.
   * Not yet checked: the stylus on the stick, savestates, a heavier 3D game.
2. **Dolphin hotkeys** — the one recompiled emulator (patch 012).
3. **PPSSPP-2021** now keeps its config and saves in /roms/psp/ppsspp-2021.
   Saves made with it before are still in /roms/psp/ppsspp and are not moved.
4. **Shutdown image** (bye.gif through ffplay in finish.sh / pause.sh): does
   it show the right way up through our SDL rotation?
5. **wifi_importer**: a wifikeyfile.txt in Tools is imported at boot.
6. Not taken, to be decided on the device: libOpenCL.so symlink (upstream
   says it breaks video previews), Dolphin Stretch option, batteryplus,
   headphone auto-switch (needs kernel work), bluealsa conf path.

## Left by the patch audit after 1.3 (2026-10-26, to do)

Every patch we carry is applied by a recipe and applied cleanly in the 1.3
build or in the build its cached binary comes from; none came up reversed or
failed. Fixed the same day: the OTA packages run the hooks of the releases
they skip (OTA.md), PPSSPP's and ECWolf's cache keys take in their patches,
retrorun's key is stored as it is looked up (it never matched: the clone's
HEAD is the miniloong branch by then), and a failed gl4es build stops the
image build. Left, each costing a rebuild of something big, so best done
with the next change there:

- **RetroArch** (one rebuild for all of it): `retroarch.sh` is not in the
  cache key; `retroarch-patch-0000` applies with fuzz 2 (patch's limit, so the
  next context change stops the build) and `0008` with fuzz 1; regenerate both
  against the tree as patched so far. `0008` only goes into the second,
  unrotated build, which the RK3562 never installs: drop it for every RK3562
  unit, as `build_retroarch.sh` does for the units other than rg52mini.
  Its "restore pristine patches" step (`git -C ... checkout -- patches/`)
  never runs: the copied submodule's `.git` points at a gitdir outside the
  copy, so it fails silently; harmless while each build gets a fresh copy.
  Our `retroarch-patch-0016` reuses the number of upstream's deleted 0016, so
  an upstream change to that file will conflict in a merge.
- **SDL2**: `sdl2-patch-0005-odroidgoa-rotate-cursor` (2023, against 2.28)
  applies with fuzz 1; regenerate it. `build_sdl2.sh` copies this repo's 0004
  and 0005 in as 0005 and 0006, next to the core builds' own different 0005
  (`drm-connector`); both edit `SDL_kmsdrmvideo.c` and the build works only
  because the glob sorts `d` before `k`. Name them as they apply. Its comment
  about "the odroidgoa skip logic in sdl2.sh" describes logic our sdl2.sh no
  longer has. `build_sdl2_ondevice.sh` applies the core patches before the
  rotation and never 0008, so a rebuild on the device loses the scaled modes:
  bring it up to date or delete it.
- **Kodi**: its key is a constant bumped by hand, and `build_kodi.sh` has no
  `verify_action`; kodi-install's "There was an issue applying" is not one of
  the messages `scripts/audit-components.sh` looks for.
- **ScummVM**: `build_scummvmsa.sh` keyed on christianhaitian's network copy
  of the recipe (v2026.4.0) while our fork built v2026.3.0 — fixed on
  2026-10-28 for it and seven more recipes (KNOWN-ISSUES, the build cache).
- **Patches without a header** (what and why): `azahar-patch-001`,
  `retrorun-patch-0002`, `ppsspp-patch-010`, `dolphinsa-patch-011`, the core
  builds' `sdl2-patch-0005-drm-connector`, this repo's `sdl2-patch-0004` and
  `-0005`, `kodi-patch-001`; and upstream's `gametank-002`,
  `flycast-patch-makefile` and `ecwolf-001` as we edited them for the A53
  (KNOWN-ISSUES.md has the why). `azahar-009` and `-010` are GammaOS's and
  upstream's code under "From: dArkOS RG52 Mini port", where 015 names
  GammaOS. A header changes the patch and so the key: add one when the patch
  is next touched.
- **Rotation patches the rotate layer made idle**: `ppsspp-patch-010` (the
  layer gives PPSSPP a landscape swapchain; KNOWN-ISSUES.md,
  VK_LAYER_DARKOS_rotate), a candidate for removal once checked on the
  device. `flycastsa-patch-004` and `dolphinsa-patch-011` went with flycast
  2.7 and Dolphin 2609a.

## Found while testing 1.2 (2026-10-18)

* **ScummVM's MIDI device** — done for 1.4: ScummVM is built with
  FluidSynth and the config names Debian's FluidR3_GM soundfont
  (KNOWN-ISSUES, "ScummVM opened every game on a dialog").
* **Speaker volume.** The DAC stays at −20 dB, which is what keeps the
  low-battery reset away (KNOWN-ISSUES). If the speaker is to get louder,
  take the battery-dependent limit the GammaOS port is writing for the rk817
  codec rather than raising the DAC.
* **`create_image.sh` packs every `*.img` in the build directory** into 7z,
  so OTA base images kept next to the build (1.1, older test builds) cost a
  few minutes and 4 GB each per build. Keep them in another directory, or
  pack only the image just made.
* **`ota/make-ota.py -o DIR`** fails at the very end, after three minutes of
  comparing, when DIR does not exist. Create it first, or let the script.
* **`retroarch32` with `parallel_n64` ignored SIGTERM** in the test harness:
  systemd killed it after 90 s. The exit hotkey was not tried; worth a look.
  (Since then the same with mupen64plus_next, 2026-10-25, see "libmali
  g29p1-5"; ES's unit now stops after 10 s.)

## Nintendo 3DS: Azahar is in (2026-10-20)

`azahar_libretro` (2126.1.2 with GammaOS's azahar-nano) with clang and PGO,
RetroArch on vulkan for it, the large screen layout, the `n3ds` alias and our
nes-box folder (KNOWN-ISSUES). Left:

* **Our part of the profile comes from one game.** GammaOS's part covers six,
  but only where their code and ours compile alike. Train ours on two or three
  more (a 3D one with many effects, a 2D one) and merge them:
  `llvm-profdata merge -weighted-input=<w>,<file>`, with weights that give each
  part about the same total count (`llvm-profdata show --detailed-summary`).
  The recipe names the file.
* **A new profile needs the chroot's paths.** clang names file-local functions
  in a profile by the path of their source. Ours was trained from a tree on
  the host, and the image builds in `/home/ark/rk3562_core_builds/azahar`
  in the chroot: the recipe's run there (2026-10-21, 573 min under qemu,
  exit 0) made a core 600 KB larger than the measured one, without the
  profile of those functions (6% of its counts). The profile now carries
  the chroot's paths, and a host build from that path matches the measured
  core (`.text` within 64 bytes). For a new one: `llvm-profdata merge
  --text`, the training tree's path replaced, merged back; ARMSX2's has the
  same treatment.
* **How well a profile fits the code:** build with `-fprofile-use=<it>
  -Wno-everything -Wbackend-plugin -Wno-error=backend-plugin -mllvm
  -pgo-warn-missing-function`, with the externals' `-w` taken out of
  `externals/CMakeLists.txt` (it reaches everything built against boost).
  clang names each function whose code changed since training (hash mismatch,
  its data dropped); their share of the profile's counts (`llvm-profdata show
  --all-functions --counts`) is what is lost. GammaOS's profile lost 44%.
* **More games.** Super Mario 3D Land was played; Castlevania's intro and
  Luigi's Mansion's start were measured (below). Check a heavier one
  (Monster Hunter, Zelda) and what speed it reaches.
* **azahar-nano's switches:** their defaults were measured, and on
  Castlevania the stereo skip and the GPU flush lag (below). Since
  006-008 (2026-10-23) the core is set up as GammaOS's frontend sets it
  (KNOWN-ISSUES, "GammaOS's settings").
* **Presenting on a thread of its own.** With both eyes drawn Super Mario 3D
  Land runs at 42 here with the GPU 64% busy, at 50-58 on GammaOS with the
  same GPU work: the core waits about 15 ms a frame inside RetroArch's
  present (acquire of the next swapchain image), and the emulation does not
  go on meanwhile. GammaOS's azahar-nano presents on its own thread and shows
  the newest frame. Right eye off (the default) it is 60 either way; a game
  heavier on the GPU would show it again.
* **The libretro core's Vulkan device has few extensions.** RetroArch lets the
  core create the device (`CreateVulkanDevice` in `libretro_vk.cpp`), and it
  enables only swapchain, image format list, external host memory, tooling
  info and custom border colour. The renderer then finds no function pointers
  for extended dynamic state or timeline semaphores and goes without
  ("disabling" in the log), so each shader pair needs a pipeline per fixed
  state, where GammaOS on Mali makes cull, depth, stencil and blend dynamic:
  more pipelines to build and cache. It also logs "Enabling extension:
  VK_EXT_index_type_uint8" and uses 8-bit indices on a device that never
  enabled it; Arm's driver takes them.
* **The JIT warm-up does nothing yet.** `azahar-patch-007` sets
  `gammaos_jit_warmup` as GammaOS's frontend does, but only that frontend
  calls `GammaJitWarm` (at boot) and `GammaJitSave` (every 30 s and at exit);
  core.cpp calls the warm-up only after its own state load, which the libretro
  core does not use. No list is written (`jit/` under the user directory).
  Calls in `retro_load_game`, `retro_unserialize` and `retro_unload_game`
  would give the faster first seconds after a boot or a state load.
* **A newer azahar-nano** means a new `azahar-patch-000`: the diff of their
  tree against the 2126.1.2 tag with its submodules, made as its header says,
  then 002-005 checked on top. (Since 2026-10-25 that is 001-018; 009 and 015
  are azahar-nano commits a newer 000 would already have.)
* **At 600 MHz**, a CPU cap used only for measuring, every build (2126.2 too)
  stops drawing about a minute into Super Mario 3D Land. Not seen at 816 MHz
  and up.

## Build: what still runs under qemu (2026-10-21)

ARMSX2 and Azahar compile with the amd64 build of the chroot's clang now
(BUILDING.md, "Compilers that run natively in the chroot"); Azahar's core went
from 573 minutes to 11 with the same code. The bootstrap and the dependency
stage, nearly four hours, come from a copy when nothing they are made of has
changed (BUILDING.md, "The chroot from the cache"). Left under qemu, largest
first, with the times of the 2026-10-19 build:

* **cleanup_filesystem.sh**, 2 h 6 min. Most of it goes to the loops over
  `needed_packages*.txt` that put back what the removal of the dev packages
  took: a dpkg -s and an `apt-mark manual` for each line, each a start of apt
  under qemu. One `dpkg-query` and one `apt-mark` for each list would leave the
  installs alone.
* **The 32-bit chroot**, 1 h 13 min: `setup_arkbuild32` debootstraps it and
  runs `build_deps.sh 32` and SDL2 in it on every build. It is only for
  building; a copy kept as the 64-bit one is would do.
* **Recipes that build with gcc.** Debian's gcc-14-aarch64-linux-gnu is the
  same compiler built as a cross compiler; whether its output matches the
  native gcc's has to be checked as Azahar's was, component by component.
* **cmake and ninja themselves**: Azahar's configure took 5 minutes under qemu.
  Kodi's 71 addons spent most of a 43-hour build there (KNOWN-ISSUES); it is
  cached, so this matters only when it has to be built again.

## Dolphin on malisx2: why it did not start (found 2026-10-26, fixed for 1.3.1)

1.3 left Gamecube out of the systems that offer MALISX2: Dolphin did not
start on malisx2. Two causes, both fixed (KNOWN-ISSUES.md, "Dolphin on
malisx2"):

- **No occlusion query pool.** Dolphin's Vulkan PerfQuery (`VKPerfQuery.cpp`)
  makes 512 occlusion queries at start and fails the backend without them;
  malisx2 on the job manager had timestamp pools only (`mali_jm_query.c`), so
  `vkCreateQueryPool` said VK_ERROR_FEATURE_NOT_PRESENT. The pool is only used
  with `PerfQueriesEnable` on (off unless a game's settings turn it on).
  Fixed twice: malisx2 0488ddb accepts the pool (every result 0 samples), and
  `dolphinsa-patch-014` runs Dolphin without perf queries when a driver
  refuses it. Found with Dolphin's log on the console
  (`-C Logger.Options.WriteToConsole=True -C Logger.Logs.VIDEO=True`).
- **A GPU fault in ND Demo.** Past the pool (a test layer handing Dolphin a
  timestamp pool), ND Demo's first draw into the EFB faulted the GPU at
  Dolphin's 48 MiB vertex stream buffer + 0xFFFFFFF0. Without geometry shaders
  and wide lines Dolphin expands lines in the vertex shader and reads the
  other end of a line as `input_buffer[vertex_id - 1]`; malisx2's v7 Vertex
  job, without IDVS, shades every index in [min, max], those no index uses as
  well, and for one of them that read was index -1. malisx2 80994d9
  bounds-checks storage-buffer reads in v7 vertex shaders (found by the
  malisx2 session).

Checked on the console with patch 014's build over the image's Dolphin
(2026-10-26): ND Demo on libmali 37 fps, no errors; on malisx2 1.3 Dolphin
starts and Metroid Prime runs (31 fps), ND Demo faults as above; on malisx2
80994d9 ND Demo 41 fps, no errors. Metroid Prime on 80994d9 with the image's
Dolphin: 39.6 fps (the malisx2 session).

**What the bounds checks cost ARMSX2**, which expands sprites in the vertex
shader too: none measurable. The image's ARMSX2 through ES's command, after a
reboot, three rounds in turn, malisx2 3a8c885 against 80994d9 (both LTO and
the profile, as the image builds it), fps by the kernel's flips:

| | 3a8c885 | 80994d9 |
|---|---|---|
| Jak and Daxter's intro, frames 300-1850 | 32.91 / 36.04 / 35.16 | 35.62 / 35.30 / 35.51 |
| God of War demo from a savestate on the ship, frames 100-1500 | 21.71 / 21.96 / 22.19 | 21.72 / 22.28 / 21.43 |

Frame times' p95 and p99 are the same (they fall on the 16.7 ms grid). The
savestate is new (the owner's earlier one went with the card's reflash):
`T:\Dump\RG52Mini\dArkOS\malisx2\states\gow-ship-20261026.p2s`, made on
libmali at the start of the ship level, Kratos standing in the rain. The
bench is in that folder's `tools/ps2-bench/` (`drv-ab.sh`).

Open: other GameCube games on malisx2; whether a game that relies on perf
queries (with `PerfQueriesEnable` in its settings) misbehaves with results
of 0.

## PS2: sound in heavy games like Azahar's realtime audio (2026-10-26: a setting was enough)

The owner's idea, after Azahar's realtime audio made a 21 fps 3DS game sound
much better (KNOWN-ISSUES.md, Azahar): in a PS2 game running below full
speed the sound breaks up the same way. Read in ARMSX2 a7ad453 (the image's):

**How the sound is paced.** The SPU2 mixes one 48 kHz sample per 768 IOP
cycles of emulated time (`TimeUpdate` in `pcsx2/SPU2/spu2sys.cpp`,
`TickInterval`), called from the IOP whenever it touches the SPU2 and from
`SPU2async`. The samples go to an `AudioStream` (`pcsx2/Host/AudioStream.cpp`,
Cubeb), which a host callback drains at 48 kHz of real time. Time stretching
(SoundTouch, `SyncMode = TimeStretch`, the default) is on: when the game runs
slow the buffer empties, its tempo follows the buffer's fullness, and the
sound slows down with the game instead of stopping. When the buffer runs dry
anyway, the stream plays silence until it has refilled 1/32 of itself
(`ReadFrames`, `m_filling`): that is the breaking up.

**The cause here: our ini set nothing of it.** `PCSX2.ini.rg52mini` had
`[SPU2/Output] OutputModule = cubeb, Latency = 100, SynchMode = 0`, the old
SPU2-X keys; ARMSX2 reads `Backend`, `SyncMode`, `BufferMS`,
`OutputLatencyMS` (`Pcsx2Config::SPU2Options::LoadSave`,
`AudioStreamParameters::LoadSave`), so it ran on its defaults: a **50 ms**
buffer target where 100 was meant, while frames here often take 80-120 ms
(God of War's ship: p95 116 ms) and the IOP makes sound only while the
emulation advances.

**Fixed with the setting (2026-10-26):** the owner listened to God of War
with `BufferMS = 150`, then 100: both sound right, and 100 stays (less
latency). The templates now carry `Backend = Cubeb`, `SyncMode =
TimeStretch`, `BufferMS = 100`; `standalone-pcsx2sa` adds `BufferMS = 100`
once to an ini made from the old template (flag `.spu2-buffer-100` next to
it, as `.selfread-defaults`), so a value set by hand stays. ARMSX2's SDL
frontend writes `OutputModule = sdl` into an ini that lacks it
(`pcsx2-sdl/Main.cpp`); nothing reads it.

Not done, as the owner decided the setting is enough; kept for the record:

**Azahar's way, for the SPU2: possible, as a patch.** Azahar makes its HLE
DSP's audio tick follow the frame time, so a slow game still makes a frame's
worth of sound per frame of real time. The SPU2's equivalent is a
`TickInterval` shorter than 768 while the game is slow: 768 x speed, the speed
from the frame time (`PerformanceMetrics::GetSpeed`, or better a loop on the
output buffer's fullness: shorten the interval while the buffer is below its
target), clamped to 1/4-1, never longer than 768 (fast forward has its own
nominal rate). What it would do:
- Voices advance per mixed sample, so pitch is right and sampled sound (sound
  effects, streamed music and speech) plays at real speed. Music a game
  sequences itself on the IOP (note on/off from its own timers) keeps the
  game's slowed tempo at the right pitch.
- Streams are read faster in emulated time: SPU2 IRQs at buffer addresses come
  sooner, so the game's IOP driver must refill sooner. The IOP is mostly idle
  and the disc far faster than an ADPCM stream, so it should keep up; AutoDMA
  input (`ReadInput`) is the same. Where it does not, stale data plays.
- Sound can run ahead of the picture (lip sync, cutscenes timed by the
  stream); an FMV player that syncs video to its audio would drop frames.
- Emulation is no longer deterministic: the IOP sees the SPU2 at timings that
  depend on the host's speed, so benchmarks between fixed frame numbers need
  it off.
It would be an option (`[SPU2/Output] RealtimeAudio`), time stretching
staying for the jitter.

## PS2: EE cycle rate below -3 (2026-10-29: in the menu for 1.4, the default unchanged)

ARMSX2 says at start that the cycle rate is not at its default and may cause
slowdown: our `PCSX2.ini` sets `EECycleRate = -3`, `EECycleSkip = 1` (from
bmdhacks 067a27d). The warning stays (the owner). -3 is the lowest the build
takes: `MIN_EE_CYCLE_RATE` in `pcsx2/Config.h`, `kMinSelector` in
`EECycleRateController.cpp`; upstream PCSX2 stops there too (50%). The arm64
recompiler counts `(5 - 2*(rate+1))/32` of a block's cycles where the default
counts 4/32 (`scaleblockcycles_calculation_for`), so the EE runs at 44% of its
clock at -3, 36% at -4, 31% at -5, 27% at -6.

Measured with a7ad453 and its profile, built with both limits at -6
(`armsx2-cyclerate-min6.patch`, the session's scratchpad), malisx2 7732b3a
with FP16 off, as ES starts it, two rounds in opposite orders, 75 s each.
fps is frames the game drew a second (SkipDuplicateFrames is on); speed is
ARMSX2's PerfLog "fps", emulated vsyncs a second, from its second line
(about 30-60 s in, after the state is loaded); 50 is full speed for these PAL
games:

| rate/skip | 0/0 (ARMSX2's default) | 0/1 | -1/1 | -2/1 | -3/1 (ours) | -4/1 | -5/1 |
|---|---|---|---|---|---|---|---|
| God of War, ship, fps 100-1500 | 22.04 | 24.53 | 24.12 | 23.49 | 22.77 | 22.50 | 23.86 |
| speed | 19.4 | 22.2 | 22.9 | 24.2 | 25.7 | 29.0 | 35.7 |
| God of War, owner's save, fps | 24.35 | 26.88 | 27.24 | 25.55 | 24.29 | 25.81 | 24.90 |
| speed | 18.5 | 21.8 | 23.0 | 24.1 | 25.0 | 30.8 | 33.4 |
| Jak's intro, fps 300-1850 | 32.21 | 32.50 | 35.11 | 36.94 | 35.35 | 37.30 | 43.52 |
| speed | 21.1 | 25.5 | 26.4 | 26.4 | 34.4 | 36.2 | 40.1 |

God of War: the lower the rate, the faster the game's own time runs (35.7
against 25.7 at -5, +39%; from the owner's save +34%) at the same frames a
second: the game draws a frame every more vsyncs, so it moves faster but no
smoother. Higher rates draw more frames of a slower game (0/1: 8-11% more
frames, 14% less speed than -3). Jak gains both: at -5 23% more frames and
17% more speed. `EECycleSkip = 1` alone is worth 8-11% of both (0/0 against
0/1). The screen at the end of every run shows nothing broken; in 75 s Jak at
-3 and -5 got further into the intro than at 0.

Not checked: whether games break with the EE this slow (their logic, cutscene
and sound timing, games that count on CPU time); only two games, 75 s each,
sound off. a7ad453's `DynamicEECycleRate` (off; AutoTune can turn it on, and
it needs `EECycleSkip = 0`) lowers the rate by up to two steps on its own.

**Taken (the owner, 2026-10-29): -4, -5 and -6 in the menu, the default
stays -3, the user chooses.** `pcsx2/patches/pcsx2-0005`: both limits at -6,
FullscreenUI's list from "27% Speed", the governor's floor kept at -3 (or it
would have taken our -3 down to -5 when AutoTune turns it on), and VU0's
micro programs charged at -4..-6 as the EE's blocks are, (5 - 2*(rate+1))/4,
where the switch in `mVUtestCycles` and `VU0microInterp.cpp` left them at
100% for a rate it did not know. The numbers above are from the scratch
patch, with VU0 at 100% below -3. With `pcsx2-0005` itself, the same way
(two rounds, opposite orders):

| rate/skip | -3/1 (ours) | -4/1 | -5/1 | -6/1 |
|---|---|---|---|---|
| God of War, ship, fps 100-1500 | 22.49 | 22.46 | 22.62 | 23.94 |
| speed | 26.3 | 29.5 | 33.5 | 38.8 |
| God of War, owner's save, fps | 24.64 | 24.54 | 25.16 | 25.06 |
| speed | 25.9 | 29.4 | 33.7 | 37.5 |
| Jak's intro, fps 300-1850 | 35.47 | 38.36 | 44.20 | 46.36 |
| speed | 34.5 | 35.8 | 40.0 | 41.5 |

-6 runs God of War's time 43-48% faster than -3 at the same frames, and Jak's
intro draws 31% more frames. The screens at the end of the runs are whole. The
menu checked on the console: the list reads 27%, 31%, 36%, 50% ... 300%, the
-5 set in the ini shows as 31%, and choosing 27% wrote `EECycleRate = -6`.

## PS2: the GPU gets the heat; what else was tried (2026-10-22)

Measured as in the next section (fps between fixed frames, builds in turn, after a
reboot), on the charger at 99%, the SoC at 67-81 C: Jak and Daxter's intro (frames
300-1850) and God of War's demo resumed from the owner's savestate on the ship
(Continue pressed at 15 s, frames 600-2000).

**Where the time goes** (`perf`, the recipe's build with a JIT perf map):
- Jak's intro waits on the EE thread: 92% of a core, half of it JIT; `spu2Mix` is
  15.6% of the thread, all IOP and SPU2 work about a third. GS is next (58-92%).
  Both run 0.44-0.47 instructions a cycle with 4-5 million L2 refills a second:
  they wait on memory.
- God of War waits on the GPU: 80-93% busy, while the thermal governor
  (`power_allocator`, its trip at 75 C) holds it at 300-900 MHz, mostly 400-700,
  and the CPU stays near 2 GHz.

**Kept: `gpu-cpucap`** (`scripts/`, started by perfmax for every game under the
performance and overclock governors, stopped by perfnorm). The CPU and the GPU
share one thermal budget. With the CPU capped at 1608 MHz, God of War's GPU
stayed at 900 MHz and the SoC at 74 C: 28.36 fps against 25.33 (+12%); the same
cap cost Jak's intro 11% (31.06 against 34.89), where the GPU is 30% busy at 900
MHz. So the cap follows the GPU: every 2 s the CPU's limit goes a step down while
the GPU is 80% busy or more and the thermal governor holds it (its devfreq cooling
device in a state above 0), and a step up while the GPU is under 60% busy or the
SoC is 5 C under the first trip. A CPU taken too low starves the GPU, whose load
then falls and stops the steps, so how far it goes is found on the device and in
the room, down to 1200 MHz at the most; the CPU's top (2208 MHz under overclock)
is put back when the game ends, before perfoc takes boost away. The top comes
from `cpuinfo_max_freq`: `scaling_max_freq` reads the thermal governor's limit
as well (policy->max on 5.10), so a game started on a hot device would have
taken the held frequency for the top and kept it after the game.
- GammaOS ported it (Android, same kernel) and measured Minecraft on a hot
  console (78-83 C), four ABBA pairs of 3-minute windows: under its "max" mode
  32.6 fps against 29.8 (+9%, every pair), the GPU at 835 MHz on average
  against 472; under schedutil 33.8 against 33.6, no difference (that scene
  waits on its main CPU thread). It runs there by default in Normal, Max and
  Overclock, and replaced the fixed 1416 MHz "3D Games" mode.
- Charging, its first version (a 1608 MHz floor, the PS2 launcher only), two
  rounds: God of War 28.02 and 28.19 against 27.03 and 24.96; Jak 34.69 and 34.60
  against 34.02 and 34.67, its limit untouched.
- On a full battery (no charging current, no throttling but in God of War's
  heaviest moments), two rounds each:

| Scene | performance | overclock | performance + cap | overclock + cap |
|---|---|---|---|---|
| God of War, frames 600-2000 | 28.91, 27.58 | 29.51, 29.28 | 28.68, 28.23 | 29.27, 28.92 |
| Jak's intro, frames 300-1850 | 34.38, 34.99 | 35.80, 35.66 | 34.23, 34.63 | 35.19, 35.98 |
| Doom 3 (the port), arrival scene | 11.38, 11.43 | 12.28, 12.26 | 11.43, 11.40 | 12.26, 12.23 |

  The cap took God of War's CPU to 1800-1608 MHz under performance and left the
  others alone: no change either way when cool. Overclock gives 3-4% in the PS2
  games and 8% in Doom 3, which waits on the GPU (1000 MHz there); under its trips
  (83/93 C) the GPU was not held, and the cap stayed at 2208.
- Doom 3 is measured on its first level (`+map game/mars_city1`), whose arrival
  scene runs on the game's clock: fps over 90 s from 5 s after the level's load.
  Its timedemo cannot be used: dhewm3 stops `demo1` with "Bad render demo token".
- The devfreq `max_freq` is the governor's limit while it throttles, so the cap
  asks the cooling device instead: one read of `max_freq` at a hot start would
  take the held frequency for the GPU's top.

**Kept: the masked GS memory clears prefetch a kilobyte ahead**
(`pcsx2-0003`). In God of War the function fell from 3.9% to 2.8% of the
samples; clearing a 640x448 frame out of the caches on the device: 468 us
against 617 (-24%), 2 and 4 KB ahead the same, `PRFM PSTL1STRM` -11%, storing
the three low bytes of each 24-bit pixel without loading (ST3 lane stores)
+37%. The memory written is the same; the frame rates do not show it (God of
War waits on the GPU), but nothing gets slower.

**Kept: ARMSX2 chooses how a draw reads its own target** (2026-10-23). The
template had `DisableFramebufferFetch = true` and `OverrideTextureBarriers = 0`
since June, set for the bmdhacks PCSX2 fork ("artifacts on Mali g29p1"). On
ARMSX2 they force its slowest road on Mali, `self-read road = copy`: every draw
that reads the target (Mali blends in the shader, so many do) copies it and
breaks the render pass. The driver has rasterization-order attachment access,
and ARMSX2 left to itself (both at their defaults, false and -1) reads the
destination in the tile: `self-read road = in-pass, driver-ordered, in-tile fetch`.
God of War from the savestate, GPU held at 600 MHz and CPU at 1608 MHz (under the
thermal limit), 10 s of Mali job trace, two rounds:

| Road | GPU per frame | fragment jobs (passes) per frame | fps |
|---|---|---|---|
| copy (the old template) | 30.8, 30.5 ms | 74.6, 73.7 | 25.2, 25.1 |
| in-tile fetch (defaults) | 26.3, 22.5 ms | 53.9, 54.1 | 26.5, 26.7 |
| barriers, no fetch (`DisableFramebufferFetch = true`, -1) | 28.9, 28.1 ms | 56.0, 56.1 | 25.5, 25.4 |

At the usual clocks, hot (76-80 C, the GPU held by the thermal governor), the
fps barely move: Jak's intro 34.60 and 34.72 against 34.11 and 34.43, God of War
27.46 and 26.88 against 27.50 and 26.08. The GPU does 15-27% less work a frame,
which shows as speed only where it is the limit, and as heat everywhere. No
artifacts on either game's screens (taken at different moments, not compared
pixel by pixel). `OverrideTextureBarriers = 0` alone keeps the copy road
whatever fetch is set to: the in-tile read comes only with barriers
(`GSSelfReadRoadPolicy.h`). Only the RG52 Mini's template is changed. An ini
already made from the old one on /roms is put on the defaults by the launcher
(`standalone-pcsx2sa`) once, on the first PS2 game after the update: it leaves
`.selfread-defaults` beside the ini, so a value set by hand later stays. A flag
rather than the ini's date against the update's: an OTA unpacks its files with
the build's times, and the clock on exFAT is local time.

**Tried, not kept** (the change against the same build without it):

| Change | Jak's intro | God of War |
|---|---|---|
| ARMSX2's IOP thread, forced on four cores | +0.8% | about -4% |
| the same, a 2 us idle watch, the largest lag | +1.3% | about -4% |
| the same, asleep until the EE is half the lag ahead | +0.7% | about -5% |
| GS multi-threading (`GSBackThreadMode = 1`) | -1.3% | -1% |
| transparent huge pages (a 5.10.226-thp kernel, `always`, shmem too) | +1.3% | -1.4% |

- The IOP thread watches the EE's watermark for 500 us before it sleeps, and the
  EE publishes every few: 64% of a core, taken from GS, MTVU and Mali's thread,
  and from the GPU's share of the heat. In God of War, which streams from the
  disc through the IOP, the EE also waits on it.
- Huge pages were in use (250 MB anonymous, GS memory's 4 MB of shmem): TLB
  misses are not what costs. The image's kernel config is unchanged.
- `spu2Mix` is flat: all 48 voices every sample.
- DDR runs at its top, 928 MHz, during games already (perfmax).
- ARMSX2's **AutoTune** (on by default, off for these measurements) has the IOP
  thread and GS multi-threading among its levers, and keeps one only where it
  measured a gain.

Left:
* **`gpu-cpucap` was measured on the RG52 Mini alone**, and in PS2 games and
  Doom 3. Dolphin's ND Demo waits on the CPU (the GPU 45% busy at 900 MHz), so
  the cap does nothing there.
* **`pcsx2-0003` changed `ClearGSLocalMemory`**, so the profile no longer fits
  it and clang builds it without; retraining would put that back.
* **`spu2Mix` runs every voice every sample**, stopped ones too (their addresses
  still move and can raise IRQs). A faster path for stopped voices has to keep
  that timing exact.

## PS2: ARMSX2 with a profile and ThinLTO (2026-10-21)

ARMSX2 replaced the old PCSX2 fork (KNOWN-ISSUES). `build_pcsx2sa.sh` builds
it with clang 19, a profile trained here (Jak and Daxter's intro, its menus,
the BIOS screens, and since 2026-10-21 the owner's play, below) and
`LTO_PCSX2_CORE`, ARMSX2's switch for ThinLTO of the
core. Measured as in the next section: fps over frames 300-1850 of the PAL
intro, the SDL frontend on Vulkan, volume 0, ES stopped, and ARMSX2's AutoTune
off (it changes settings while the game runs). Each series rebooted first and
ran its builds in turn, three rounds; the base is ARMSX2 with the old recipe's
flags (`-march=armv8-a -moutline-atomics -O3`) and no profile.

| Build: the base flags and | Series 1 | 2 | 3 | 4 |
|---|---|---|---|---|
| nothing (base) | 31.59 | 31.29 | 31.54 | 31.49 |
| the profile | 33.23 | | | |
| the profile, `-mcpu=cortex-a53` | 33.98 | 33.75 | 33.70 | 33.30 |
| **the profile, ThinLTO (the recipe)** | | **34.28** | **34.06** | **34.09** |
| the profile, ThinLTO, `-mcpu` | 33.95 | | | |
| ThinLTO | | 31.96 | | |
| ThinLTO, `-mcpu` | 31.90 | | | |
| the IOP thread forced on | | 30.58 | | |
| the profile, `-mcpu`, `-mno-outline-atomics` | | | 33.70 | |
| the same, `-fomit-frame-pointer` | | | 33.76 | |
| the profile, ThinLTO, `-mno-outline-atomics -fomit-frame-pointer` | | | | 34.05 |
| the same, `-mcpu` | | | | 33.98 |
| the profile, `-mcpu`, SPU2 `LightweightMode` | | | 34.13 | |

The recipe's build is 8.6% faster than the base (34.1 against 31.4); the
profile alone gives 7%, ThinLTO without it 1.5-2%. Runs of one build spread
by 1-2%.
- **`-mcpu=cortex-a53`** gains nothing with the profile and LTO; without a
  profile it lost 2.4%. `-mno-outline-atomics` (the A53 has no LSE, so every
  atomic goes through a helper that checks for it) and
  `-fomit-frame-pointer` changed nothing measurable either.
- **The IOP thread** (forced on in a test build; ARMSX2 turns it on only with
  six cores or more) costs 2% on four: it takes a core from the EE, GS and
  MTVU threads.
- **`LightweightMode`** (`[SPU2/Output]`, skips the reverb) gained 1.3% over
  the same build in the same series, within the spread, and changes the
  sound. Not set.
- **Where the time goes** (`perf`, 30 s of the intro, the profile build): the
  EE thread 40% of the busy samples, GS 28%, MTVU 12%, Mali's driver thread
  9%. About half the EE thread is ARMSX2's own code, and 39% of that is audio
  (`spu2Mix` 25%, reverb 6%, TimeStretch 4%). In GS: vertex kicks 13%,
  `GSState::Transfer` 13%, `GSRendererHW::Draw` 10%, and about 12% in the
  software rasteriser, which the GameIndex's `cpuSpriteRenderBW: 4` for Jak
  asks for (a correctness fix, kept).
- **Hot device.** It was on the charger: the SoC at 80-82 C during runs, the
  GPU held at its lowest step by the thermal governor and the CPU at times at
  1608 MHz. The absolute numbers belong to that; the builds ran in turn, so
  the differences hold.
- **Off the training.** A savestate in Jak and Daxter's opening cutscene
  (after New Game, in the game engine; `armsx2-sdl --state-from-file`
  resumes it), frames 100-800, three rounds: base 21.29, the profile and
  `-mcpu` 23.84 (+12.0%), the recipe's build 24.48 (+15.0%); over frames
  100-1500, which the first base run after the reboot fell short of, +11%
  and +14%. The profile helps more here than on the intro it was trained
  on. The cutscene is heavier than the intro (21-24 fps).
- **Presses from a script have to be written again while held.** The
  joystick driver polls B, X, Y and the d-pad and reports their real state
  every 20 ms, so a press written into its evdev node (as rg52remote does)
  is released again within 6-20 ms; A and Start it leaves alone. ARMSX2 saw
  some of those short presses and Jak none, so at first the d-pad, Select+X
  and Select+R1 seemed dead. Written again every 3 ms while held, they work:
  the pause menu (`pcsx2-0001`) opens on ARMSX2 started from ES, stays, and
  Resume goes back to the game.
- **The owner's play in the profile** (2026-10-21). An instrumented build
  (`-fprofile-generate`, the same tree at `/home/ark/armsx2`) was
  bind-mounted over `/opt/pcsx2/bin/armsx2-sdl` behind a script that runs it
  under another name: killer_daemon's hotkey sends `pkill` and at once
  `kill -9` to what `pidof` still finds by that name, and a profile is
  written only on a clean exit, so the script passes the SIGTERM on and
  waits. The owner played Jak and Daxter, God of War's demo, Burnout 3's
  demo and the Sopranos disc (its BIOS screen, a minute and a half). Merged
  with `-weighted-input` so that the old profile, Jak, God of War and
  Burnout have about the same total count each (17, 10, 26, 34; Sopranos 10,
  its own size). The core is 340 KB larger with it. Three rounds of each in
  turn after a reboot, old profile against new: Jak's intro (300-1850)
  34.57 and 34.31, Jak's opening cutscene (100-1500) 24.86 and 24.79, God of
  War on the ship from the owner's savestate (600-2000) 26.25 and 26.36: all
  within the spread. What other games run hot, the profile from Jak's intro
  covered already: the recompilers, GS, SPU2 and the VUs are ARMSX2's own
  code, and a game's own code runs as JIT output, which clang never sees.
  The new one is kept: it is trained on the owner's games and costs nothing
  measurable. The God of War savestate was made in the game's pause menu
  (50 fps, PAL's limit); the harness presses Cross 15 s after the launch.

Left:
* **The chroot's build was not measured.** The recipe ran in the arm64
  chroot on 2026-10-21 with the amd64 clang (`xtool.sh`), in 7 minutes.
  With the first profile its binary (clang 19.1.7) was 3.7 KB larger than
  the host cross-build of the same tree, profile and flags (19.1.1) that
  the numbers above are for.
* **Burnout 3's demo disc** opens from its `.bin` (the owner played it,
  2026-10-21), but from its `.cue`, which ES lists too, ARMSX2 exits at
  once (earlier: "Opening CDVD... VMManager::Initialize failed"). Not
  looked into.
* **No `patches.zip`.** ARMSX2 logs "Failed to open patches.zip. Built-in game
  patches are not available": PCSX2's per-game patches (pcsx2_patches) are a
  separate download, not in the source, and the old fork's build lacked them
  too. GameIndex.yaml's fixes do apply.

## Heavy emulators: compiler flags measured, none kept (1.3, 2026-10-20)

Measured on the 1.3 test build (OTA from 1.1), volume at 0, no input, ES
stopped. Frames are counted by the kernel (ftrace `drm_vblank_event_delivered`,
one per flip of GL, RetroArch KMS or Mali's Vulkan display alike); it matched
Dolphin's own counter.

| Game | Emulator | FPS | Busiest thread |
|---|---|---|---|
| PS2 Jak and Daxter (PAL), intro | PCSX2 | 42-48, 17-18 in its heaviest part | EE 95% |
| GC ND Demo | Dolphin | 40-41 | CPU 86%, video 76% |
| PSP FF Anniversary | PPSSPP | 60 | 46% |
| DC Crazy Taxi | flycast | 60 | 67% |
| Atomiswave kov7sprt, attract | flycast | 60 (30 on its title) | 79% |
| N64 AeroGauge | RetroArch32 parallel_n64 | 59 | 55% |

Only PS2 and GameCube run below full speed. `perf` (linux-perf from Debian
installed on the test device, not in the image) splits them:
- **PCSX2:** the EE thread is about half its own compiled code and half JIT.
  The compiled half is flat: the top function, `spu2Mix`, has 4.6%.
- **Dolphin:** the CPU thread, which holds the frame rate, is about 90% JIT.

**PCSX2 builds**, against the image's (gcc 14, `-O3 -march=armv8-a
-moutline-atomics`), in fps over frames 300-1850 of the intro:

| Build | Difference from the image |
|---|---|
| clang 18, same flags | -3% |
| clang + ThinLTO | -2% |
| clang + ThinLTO + `-mcpu=cortex-a53` | -1% |
| clang + full LTO + `-mcpu=cortex-a53` | 0% |
| clang + `-mcpu=cortex-a53` | -4% |
| clang `-O2` | -5% |
| gcc 14 + `-mcpu=cortex-a53` | -3% |
| gcc 14 full LTO | starts and exits at once (below) |

Spread between runs of one build is about 1.5%.
- **gcc LTO** links only with `common/FastJmp.cpp` built without LTO, because
  GCC LTO drops the symbols of its top-level asm. The binary then exits at start
  without a line of log. Not pursued, since clang's LTO gained nothing.
- **How it was measured.** Fps averaged over a wall-clock window cannot compare
  builds on a scripted intro: the faster one reaches a heavier part sooner, and
  a run at 1.6 GHz scored the same as at 2.0. Fps between fixed frame numbers
  of the deterministic intro does see the clock: -17% for -20% of it.
- **Thermal.** Runs go back to back only after the SoC cools below 62 C. The
  thermal governor holds 75 C, and a warm device lost 3% within an hour.

**Dolphin** is built with LTO already (`ENABLE_LTO=ON` in its recipe). The same
build with `-mcpu=cortex-a53` added ran the ND Demo at 41.0 fps against the
image's 41.5 (two interleaved runs each, spread about 1%): no gain either.

**Decision: no flag changes in the build scripts.** On the in-order A53 these
emulators wait on memory and JIT code, and the compiler does not reach either.
PGO stays the one untried option for them. It needs a training run in real
gameplay with the owner, and the flat profile leaves it little to work on.
Azahar (3DS) got it: a profile trained here took azahar-nano from 24.7 fps to
30 (above; the GPU's limit then, half speed, until GammaOS's settings). So did PS2, with ARMSX2 in place of the old fork:
the profile and ThinLTO gave it 8.6% on the intro (above).

## PSP and Dreamcast: nothing to fix (2026-10-23)

Azahar and ARMSX2 had GPU settings for Mali that only their Android builds, or
an old template, left out here. PPSSPP 1.20.4 and flycast 2.6 have none:
- **PPSSPP:** its Mali workarounds go by the GPU's vendor (a tiler, two
  driver bugs), so they apply on Linux as on Android. The one Android-only
  rendering change, dropping destination alpha, is for unbuffered rendering
  (`SkipBufferEffects`), which is off.
- **flycast:** on Android it adds loading its own driver for Adreno, nothing
  for Mali, and the present mode is FIFO on ARM there as here.
- **`ppsspp.ini.rg52mini`** still has keys 1.20.4 no longer reads:
  `ForceMaxEmulatedFPS = 30`, `ShowFPSCounter`, `RenderingMode`,
  `DisableSlowFramebufEffects`. They do nothing; `InflightFrames = 2` (its
  default is 3) is read.

Measured as ES starts them, cool (58-67 C, no throttling, GPU 900 MHz, CPU
2016), 8 s windows of Mali job and flip trace, the screen grabbed after each:

| Game | Emulator | Scene | fps | GPU busy | GPU per frame | Busiest thread |
|---|---|---|---|---|---|---|
| Crazy Taxi | flycast, Vulkan, 640x480 | attract run in the city | 59.6-59.8 | 47-62% | 7.9-10.3 ms | 78-81% (render 39-44%) |
| Ape Escape Racing (demo) | PPSSPP, Vulkan, 2x | a race | 60.0 | 29% | 4.8 ms | 28% |
| Hot Shots Golf 2 (demo) | PPSSPP, Vulkan, 2x | on the course | 60.1 | 30-32% | 5.0-5.3 ms | 25-27% |
| Street Fighter Alpha 3 Max | PPSSPP, Vulkan, 2x | a menu (2D) | 60 | 22% | 3.6 ms | 9% |

All at full speed with room, so `InflightFrames` was not compared. flycast's
SH4 thread is the closer limit: in a heavier Dreamcast game the CPU runs out
before the GPU. The card's games are light ones; nothing like God of War:
Chains of Olympus or Shenmue was measured. A game that runs slow here is
compared with GammaOS on the same card, as Azahar was.

## Forks of the emulators read for speed (2026-10-24)

The emulators' forks and copies were read for speed changes: Chinese mirrors,
handheld vendors' trees, Dolphin's MMJ/MMJR/MMJR2(-VBI) and Ishiiruka, the
ROCKNIX/Batocera/Knulli package trees, and ARMSX2's 190 forks. Most of them
change only the UI.

### Settings, measured: none changed

Measured on 1.3 (20261024000117, by OTA from 20261020063016), as ES starts the
games, volume 0, cooled below 62 C before each run. Each figure is two runs
after a warm-up, fps between fixed frames.

* **ARMSX2 spin wait.** `WAIT_SPIN_MICROSECONDS` (`common/HostSys.cpp`,
  default 50 us) was tried at 2 and 10 us. A shorter spin does not help and
  costs Jak 2-3%.

  | Scene | 50 us | 2 us | 10 us |
  |---|---|---|---|
  | God of War savestate, frames 600-2000 | 29.69 | 29.69 | 29.52 |
  | Jak's intro, frames 300-1850 | 34.93 | 33.81 | 34.18 |
* **N64, standalone GLideN64.** The template's `[Video-GLideN64]` has
  `configVersion = 26`, the plugin's own is 29. On its first run GLideN64
  deletes the section and writes its defaults. Of the template's values, only
  two differ from those defaults and are not set by `n64.sh` with `--set`:
  `EnableLegacyBlending = 1` and `BackgroundsMode = 0`. Both are slower here.

  | Config (AeroGauge, frames 600-1800) | fps |
  |---|---|
  | GLideN64's defaults, as after the reset | 59.36, 59.36 (the game's cap) |
  | The template as version 29 (`MaxAnisotropy` renamed `anisotropy`, `EnableNoise` dropped) | 57.38, 57.56 |
  | The same with `EnableHybridFilter = False` | 59.02, 58.78 |

  The template stays as it is: the reset drops only the slower values. The
  first run on a fresh image compiles shaders (54.8).
* **Dolphin, ND Demo.** Frames 300-2400; base runs spread 2.7%. Nothing to
  change.

  | Setting | fps |
  |---|---|
  | As shipped | 45.76, 44.54 |
  | `EnableGPUTextureDecoding = True` | 45.82, 45.36 |
  | `ArbitraryMipmapDetection = False` | 44.97, 45.71 |
  | `SyncOnSkipIdle = False` | 43.92, 44.70 |

  The SoC reached 81-84 C by the end of each 75 s run.

### Code from them, taken (2026-10-24, in 1.3)

Each measured on the device against a build made the same way without it (the
A/B builds are not the image's: Dolphin and ARMSX2 cross-built with clang,
without LTO or the profile; Yaba Sanshiro in a trixie chroot with GCC 12, as
the image).

* **Saturn, Yaba Sanshiro: it crashed.** Resident Evil died with SIGSEGV about
  10 s in, BIOS or not. Patch 13: the dynarec kept values in w4-w6 across
  memory calls (devMiyax fixed most of it upstream in 57af8f86; AND_B, XOR_B
  and one register of LDC_SR_INC only here). Patch 12: the sound thread's
  timed wait got `ctime()`'s string as its deadline, failed with EINVAL and
  polled. Resident Evil, 90 s:

  | | fps | process | sound thread | 4 cores busy | end |
  |---|---|---|---|---|---|
  | 13 | 59.8, 59.9 | 186-195% | 98% | 49-51% | 78 C |
  | 12 + 13 | 59.9, 59.9 | 105% | 17% | 28% | 73-75 C |

  The review's other report, a wrong decode of a backward `bra`, was not
  checked.
* **Dolphin: patch 013**, PR #12319 (conditional branches stay in near code)
  and the `dcbz_l` line of #13929. ND Demo, frames 300-2400, three runs:
  45.33, 45.28, 44.81 without; 47.30, 47.36, 47.20 with #12319 (+4.8%);
  46.84, 47.12, 47.36 with both. The image's own build (GCC, LTO) did 45.33,
  45.61, 45.86. Dolphin builds under qemu in the image, about 3 hours.
* **N64 in RetroArch: `mupen64plus-ThreadedRenderer = "True"`** in the image's
  core options, and for updated devices `ota/hooks/1.3-mupen64plus-threaded.sh`
  adds it unless the user has set it. mupen64plus_next had no options set:

  | | AeroGauge 600-1800 | Zelda's title 600-2400 | 1200-3000 |
  |---|---|---|---|
  | defaults | 53.86, 54.06 | 53.05, 53.10 | 51.80, 51.80 |
  | ThreadedRenderer | 56.92 | 56.11, 56.11 | 57.42, 57.45 |
  | EnableCopyDepthToRDRAM Off | 55.05 | | |
  | HybridFilter False | 53.82 | | |
  | 320x240 | 56.52 | | |
  | all four | 56.97 | | |

  Only the thread was kept: copying depth off breaks what reads it (Zelda's
  sun), 320x240 is a smaller picture. RetroArch exits on SIGTERM in 0.6 s with
  it (a SIGTERM to RetroArch alone; a stop of its whole unit did not end it,
  see "libmali g29p1-5"). The standalone GLideN64 still does 59.36 in AeroGauge.
* **Azahar: patches 009 and 010.** 009 is GammaOS's three azahar-nano commits
  of 2026-10-20 (frame pacing after a long frame; the right-eye skip no longer
  drops the bottom screen's draws in Super Mario 3D Land). 010 is upstream's
  SIMD texture codec (9ee6bc70f, PR #2564). Super Mario 3D Land from its
  savestates, with the same profile: slot 0, 60 s, 58.47 and 58.62 without,
  58.42 and 58.68 with 009; slot 9, 40 s, about 59.3 either way, with 010
  too. The blue bottom-screen frames 009 fixes did not show in our slot 9
  even without it (0 of 160 screen grabs). Neither showed a gain here; the
  codec is for texture loads, which these scenes do not have, and both were
  taken as fixes and for other games. 011, from a user's report: the core
  swapped the screens on L3, the switch under the RG52 Mini's left stick, which
  a hard push of the stick closes (a key log while playing caught L3 down for
  0.7 and 3 s). They swap on the Back key now (KEY_FN in the device tree,
  BTN_TRIGGER_HAPPY1 from the pad), which nothing else uses; the RetroPad has
  no button for it, so the core reads its state from the pad's event device
  (EVIOCGKEY). Checked on the device with Super Mario 3D Land: Back swaps and
  swaps back, L3 does nothing, Select still pauses. The build started on
  2026-10-24 at 13:50 has an earlier version of 011 (Select+R1, Select seen
  by the game); the next one has this.
* **ARMSX2: `pcsx2-0004`**, the frame limiter of deurefdfgdf's BKFT fork
  (4bdbd0a23e, the limiter only): it sleeps to 200 us before its deadline
  instead of spinning up to 2 ms twice a frame. The Sopranos' disc in the
  BIOS's browser at full speed, four runs each: the EE thread 46% of a core
  before, 38-39% after; the process 80-81% against 73-74%; 50.48 fps both.
  It does nothing in games that run slower than full speed.

### Code from them, not taken

* **Dolphin.** We ship e6583f8 (5.0-18516, February 2023), 7,308 commits
  behind master. No fork carries speed work that upstream lacks.
  * The real gain is upstream's ARM64 JIT work since then. Moving to it is
    2-4 days: forward-port patches 006/007/011 (DRM) and 012 (hotkeys), and
    set `AccurateFmadds = False`.
  * Next backports to try: the dispatcher series from #11737.
  * Measure with `PerfMapDir=/tmp` and perf.
* **ARMSX2.**
  * IOP recompiler fast paths: koundinyalanka1 61deb199d6 and 9c28867a00. They
    do not apply to our tree (2,600 lines, a port of 1-2 days), and the IOP is
    a small part of the CPU thread: the JIT of EE and IOP together is 19% of
    all samples in Jak.
  * A rebase onto ARMSX2's master, 29 commits, among them a CPU sprite path
    (85ce3417f1). The profile must be trained again after it.
  * Cached readback memory on Mali: one line, but perf first.
  * Worth watching: bmdhacks/malisx2, Mesa's Vulkan on kbase, at parity with
    libmali. (Since 2026-10-25 in the image as a choice beside libmali, see
    "malisx2 measured against libmali".)
* **Azahar:** dekopon c5e09fe36 (`IsReadOnlyMemory`); tico dcfb1027c,
  658038a14 and 6cd2fb7b0.
* **N64 in RetroArch:** its GLideN64 builds without NEON; the makefile change
  is about 5 lines.
* **Left alone.**
  * DuckStation: its licence forbids shipping a patched build (and
    pre-configured settings: KNOWN-ISSUES, DuckStation).
  * PPSSPP: wait for 1.21.
  * The libretro flycast is 4c293f3, from 2022.

## Keys: L3 closes by itself, Home toggled Bluetooth (2026-10-24, in 1.3)

The case names two buttons the code does not:
* Home is BTN_MODE, SDL's button 10, and RetroArch's hotkey enable.
* Back is BTN_TRIGGER_HAPPY1, SDL's button 17. Nothing else uses it.

The problems found:
* **L3 closes by itself.** L3 is the switch under the left stick. In two key
  logs taken while the owner played, it closed by itself when the stick was
  pushed hard: once for 0.7 s, once for 3 s.
  * Azahar swapped its screens on L3. Patch 011 moves that to Back (above).
  * DraStic opened its menu on L3. `drastic/hotkey`, an LD_PRELOAD library
    that the launchers load when it is there, puts the menu on Home+X, the
    combination that opens RetroArch's menu. It wraps SDL_PollEvent:
    * Home+X goes to DraStic as L3, still the menu's key in `drastic.cfg`.
    * A real L3 is dropped.
    * While Home is held, no button reaches the game.
    * Home alone still swaps the screens.
    Checked on the device with Diddy Kong Racing DS:
    * L3: nothing.
    * Home alone: the screens swapped.
    * The owner's Home+X: the menu.
* **Home toggled Bluetooth.** ogage's rg503 branch runs `bttoggle.sh` at
  every BTN_MODE press, so each Home+X for RetroArch's menu turned Bluetooth
  on or off. About 15 toggles are in the journal of half an hour of play.
  `ogage/patches/0001` removes it. Bluetooth is still switched in Options >
  Bluetooth.

## Azahar: save states in RetroArch (2026-10-24, in 1.3)

The owner found saves and loads slow. Measured with Super Mario 3D Land: its
state is 292 MB serialized and 23 MB compressed. A test launched RetroArch with
network commands and timestamped logs (SAVE_STATE and LOAD_STATE twice).

|   | Before | After (patch 012) |
|---|---|---|
| One serialize and compress | 9 s | 1.4 s |
| A save | about 27 s (3 of them) | 1.4 s (1) |
| Deserializing a load | 3.5 s | 1.9 s |
| A load (2 states, then the load) | about 30 s | about 5 s |
| Free memory after 2 saves and 2 loads | 47 MB, 843 MB in swap | 234 MB, no swap |

Where the time went:
* RetroArch 1.22.2 asks the core for the state's size two or three times per
  save. Before a load it asks again only to see that the size is not 0
  (`command_event_main_state`), and then saves its undo buffer. The core made
  a whole new state for each question.
* Patch 012 makes one state per frame, and it streams RetroArch's buffer path
  through zstd: level 1 on four threads while serializing, decompressed while
  deserializing. GammaOS had done this only for its file path. The patch's
  header has the details.

Measured, not changed:
* zstd level 3 takes 3.5 s for a 10% smaller file.
* RetroArch's own rzip of the state file runs on its task thread after the
  save: no pause, and 3% smaller files.

Two of a load's 5 s were RetroArch's: the size check and the undo buffer,
1.4 s each.
* `retroarch-patches/retroarch-patch-0024` skips the size check for loads,
  which should make a load about 3.5 s. 1.3 has it; not measured yet.
* The undo buffer stays: it is a feature (Undo Load State).

## Azahar: Castlevania: Lords of Shadow – Mirror of Fate (2026-10-25, in 1.3)

The owner found it slow. Measured on its intro, a cutscene in the game's
engine: frames 60 to 660 after a RetroArch save state, each run from the same
state, with a uprobe on `retro_run` counting emulated frames. The console was
on the charger, so the CPU ran at 1.3 to 2.0 GHz.

| | fps | GPU busy |
|---|---|---|
| Before | 9.2 | 95% |
| GammaOS's `stereo_skip` | 11.1 | 43% |
| and `async_gpu_flush_lag=24` | 14.4 | 57% |
| and no copies out of the right eye (patch 013) | 20.1 | 75% |

Where the time went:
* The game draws the two eyes object by object, each draw into the other
  eye's target. On Mali every switch ends a render pass: about 330 a frame.
  `stereo_skip` (a GammaOS switch, off by default) learns the right eye's
  target and leaves its draws out.
* The game flushes the CPU cache before each of its command lists, about 15 a
  frame. The core drained the GPU worker at each flush, so the emulation
  thread waited half the time. `async_gpu_flush_lag=24` leaves up to 24 jobs
  queued instead. With no wait at all it ran the same; a lag of 8 gave 11.7.
* Each eye's image is copied into one texture for post-processing. The right
  eye's copy went to the CPU, which first downloaded the left eye's copy from
  the GPU and had it uploaded again, three times a frame. Patch 013 leaves such
  copies out while `stereo_skip` holds a right eye target.

Patch 013 set both switches for this title (000400000009E500, the owner's
copy) in `retro_load_game`, unless the environment already set them.
Grabs at frames 700, 1300, 1900 and 2500 match the build without any of this.
The image with 013 (20261025042625) ran it at 22.4 / 21.1 fps (frames 60-360 /
360-660), GPU 82% busy.

Patch 014 makes the two switches core options, Interleaved Right Eye Skip and
GPU Flush Lag (KNOWN-ISSUES.md, Azahar), at Auto by default: on for the titles
in `title_switches` (`core_settings.cpp`), off for the rest. The core reads
them at every display transfer and cache flush, so a change in the menu
applies at once; turning the skip off forgets the learned target. Measured
with the 014 core over image 20261025042625: Auto 21.4 / 20.9 fps (the log
says "stereo_skip 1, async_gpu_flush_lag 24" for the title), both Disabled
in the game's options file 9.2 / 9.3; frame 700 the same in both. The owner
chose options over turning them on for every game: a misjudged target hides
what the screen shows, and the flush lag touches every game.

Open:
* Only the intro was measured; the game itself was not played.
* The game's other releases have other title IDs. Add them to
  `title_switches` when known.
* `stereo_skip` may help other games that draw the eyes interleaved (GammaOS
  wrote it for Metroid: Samus Returns). It is off by default there. No other
  game was tried with either switch.

## Azahar: GammaOS's azahar-nano after 8d68e5ecaab, read (2026-10-25)

GammaOSNextDistribution-14 `develop` up to f0b2a23405e: 12 commits touch
azahar-nano, the core (`gammaos/emulators/azahar-nano`) or its Android
frontend (`frameworks/base/cmds/azahar-nano`). 43f1f504258, cfd0c22df98 and
afb19857d2c are patch 009 already.

Taken:
* **e842af2339c**, patch 015: a draw into an offscreen target (a texture the
  game composes once, Pokemon Sun's skins) is never left out while its
  pipeline compiles; the hybrid path left it out with a cold cache, as it
  did for the bottom screen before 009. Applies as is. On the console:
  Castlevania 21.1 / 20.6 fps, Super Mario 3D Land 59.9, as before.
* **dbb613ef825**, cheats, the database and the merge: patch 017, with a
  folder shared with GammaOS (`ROMs/cheats/3ds/azahar`, agreed with the
  GammaOS session; they take the same path when they port the commit) and
  RetroArch's cheat menu instead of their frontend's (KNOWN-ISSUES.md,
  Azahar).
* **0e442662ca4**, realtime audio only: patch 016 makes it a core option,
  on by default: the owner listened to Castlevania's intro both ways under
  RetroArch, and with it the sound is much better.

Not taken:
* **0e442662ca4**, "performance defaults on" (frontend). Of the five values
  it seeds, hardware shaders and the disk shader cache are on here already
  (core options); audio stretching stays off because RetroArch paces the
  audio. Skip duplicate frames would skip the present when the game has not
  drawn a new frame, but here that skips the frame handed to RetroArch too,
  and the core would have to hand it a dupe (`video_cb(NULL)`); the gain is
  the core's present pass, about 1.2 ms, in 30 fps games only. Realtime
  audio is an option now (above). The CPU clock ladder fixes their menu;
  ours is in order.
* **c88869e5bf1**: malisx2 (bmdhacks/malisx2, a Mesa-based Vulkan driver
  over kbase, written for ARMSX2) bundled and the default on the Mali-G52.
  It accepts job-manager kbase from UK 11.22; the console's is 11.47 (DDK
  g25p0), and it has a Linux build (ROCKNIX, VK_KHR_display). Not measured
  anywhere on the RG52: a project of its own (open). (Since then measured
  here and built by the image as a choice, libmali staying the default: next
  section.) GammaOS measured their
  build on the RG52 under Android: Castlevania 16.7-17.0 fps against
  17.4-20.0 on libmali, Super Mario 3D Land the same at 60 (+9% uncapped),
  200 MB more memory, and lmkd killed the emulator once.
* Frontend and Android only: the DRM presenter's pacing (9a28c805178,
  f66a67211f5), a start screen (aeb2ab4a6fe), colour matrices (076b8fcef48),
  panels and sleep (e2c4505db64, f0e49ef952a), menu labels, data folders.

## malisx2 measured against libmali (2026-10-25)

malisx2 main eae9663 (0.2.3, Mali-G52 support merged from GammaOS's g52-azahar
branch, nothing of theirs left out), built on the WSL host: clang-19 and lld
against the azroot sysroot, rustup's stable rustc, bindgen 0.71.1, meson 1.12
(`~/rg52/malisx2-work`, cross file `darkos-aarch64.ini`); one change of ours,
the deviceName "Mali-G52 (malisx2)". It runs on the console's kbase (g25p0, JM
UK 11.47, negotiated 11.36; 64-byte atoms after the kernel refuses stride 72;
no JIT zone). Its Linux WSI presents through VK_KHR_display with libmali's GBM.

| | libmali | malisx2 |
|---|---|---|
| Luigi's Mansion, ~390 pipelines compiled at boot (preload) | 24.3, 25.4 s | 3.8, 3.9 s |
| the same, cold cache, flips in the first 120 s | 4904-4988 | 5380-5487 |
| Castlevania's intro, fps (frames 60-360 / 360-660) | 21.2 / 20.3 | 14.2-16.9 / 15.7-16.0 |
| Super Mario 3D Land, fps | 59.8 | 59.7-59.8 |
| God of War demo (ARMSX2), fps | 42.2 | 40.0 (CPU 54% against 70%) |
| ND Demo (Dolphin) | 44.5 | does not start |

Pictures match (Super Mario 3D Land's first run on malisx2 showed Mario as a
dark shape for the frames his pipeline was still compiling, as Azahar's hybrid
path draws). Azahar takes its vertex-depth shaders on malisx2, which has
shaderClipDistance, so its shader caches there are separate (`*_vd_hs.vkch`).
vulkaninfo: Vulkan 1.1 against 1.3; 24 device extensions against 135 (none of
dynamic rendering, synchronization2, timeline semaphores, extended dynamic
state, buffer device address, 8/16-bit storage, float16, robustness2, custom
border colours, transform feedback; it adds memory_budget and push_descriptor);
no geometry or tessellation shaders, logicOp, wide lines, shaderInt64; 16
vertex attributes and bindings against 32; one queue against two.
How it is chosen: KNOWN-ISSUES.md, "Vulkan: Arm's driver or malisx2".

Done since:
* The image builds it (`build_malisx2.sh`, 2026-10-25): xtool's clang for the C
  and C++, the chroot's rustc under qemu for kraid (Valhall's Rust compiler,
  which the build cannot leave out), bindgen 0.71 from Debian, meson 1.12.1
  from its release tarball (trixie's 1.7.0 dies writing the build of Mesa's
  structured-sources Rust crate: "'CustomTargetIndex' object has no attribute
  'subdir'"), Mesa fetched at the commit malisx2's meson.build pins. Tried on a
  dArkOS root mounted as the chroot: 33 minutes from nothing, cached by commit,
  recipe, patches and profile after that.
* Its compiler on the CPU, see "malisx2's compiler: LTO and a profile" below.
* ARMSX2's draw path on v7 (the fork's f35bacc, in 1.3.1; the malisx2 session,
  its docs/changes.md): fewer compute copy jobs per draw, a stage's tables and
  FAU block kept when a new pipeline would build them the same (copies per
  draw: God of War 2.53 to 2.12, Jak and Daxter 1.97 to 1.82), the same
  picture. Against b1c114a, three rounds each: God of War 4.5% faster (frames
  100-1500; 7.1% over 800-1500), Jak 1.3%, each on its own profile; 0.7% and
  1.4% with neither profiled. Luigi's Mansion's 269 pipelines compile in
  2.93 s against 2.92.
* On top of it the fork's 38e50a5 (b74141a's code, 2026-10-27; in 1.3.1): v7
  descriptor tables without images and the uniform words filled on the CPU,
  the words read from uncached memory in runs by memcpy
  (`mali_v7_host_ubo_run` 0.95% of Jak and Daxter's samples before, under
  0.3% after); forward pixel kill kept for draws that read no colour
  attachment under rasterization order access (`MALISX2_V7_FPK_ROA=0` for
  the old way); AFBC for textures too (`MALISX2_V7_AFBC_TEX=0`; God of War
  reads 8-9% less texture data from memory). With the profile retrained on
  it, against f35bacc on its own, three rounds: Castlevania 16.39 to 16.77
  fps (frames 60-360) and 15.68 to 15.90 (360-660), God of War on the ship
  21.71 to 22.09 (100-1500), Jak and Daxter in game 15.46 to 15.59
  (100-800), each faster in every round; Jak's intro and God of War from the
  owner's save within the spread. The same picture. Pipelines compile as
  fast as before (compbench, malisx2's cache deleted, builds in turn):
  Castlevania's 415 in 8674 ms against 8650 (+0.3%, four runs each), Luigi's
  Mansion's 288 in 2991 against 2970 (+0.7%, two runs), within the spread.
* Then 5eccf34 (d2afec5's code, 2026-10-27; in 1.3.1): the tiler's hierarchy
  mask on the G52 is 0x0a, bins of 32 and 128 px as libmali uses, instead of
  0x82 (`MALISX2_JM_HIER_MASK=130` for the old one), and D32 depth
  attachments are AFBC (`MALISX2_V7_AFBC_DEPTH=0` to turn it off). Three
  rounds, both together: God of War on the ship 1.7-2.7% faster, from the
  owner's save (100-800) 1.9%; Jak and Daxter and Castlevania within the
  spread. Tiler work 16% less, tile writes to memory 20% less, texture reads
  14-17% less. The profile stays the one trained on b74141a.
* Then 262ad73 (98a8fdf's code, 2026-10-28; for 1.4): IDVS on v7, the
  compiler splits a vertex shader into its position and varying parts and a
  draw is one Indexed Vertex job instead of Vertex + Tiler (1.2 jobs a draw
  in God of War instead of 2, GPU per quad 1-2% less). Castlevania 1.9 and
  5.6% faster (two rounds), God of War and Jak and Daxter within the spread
  (-0.9 to +1.3%), the same picture. `MALISX2_V7_IDVS=0` turns it off and has
  its own pipeline cache; the pipelineCacheUUID changes, so games compile
  their pipelines again once. Not tried yet with PPSSPP, flycast or ports.
* Where Castlevania's time goes on it, see below. The vertex side is the work:
  IDVS and descriptors/uniforms written by the CPU, in malisx2 (upstream's to
  do).

Open:
* GammaOS measured it under Android: no gain, 200 MB more memory, lmkd killed
  the emulator once. Here RetroArch's peak was 1067 MB against 1132 on libmali.

### Where Castlevania's GPU time goes on malisx2 (2026-10-25)

Both drivers through the same window: kbase's `mali_job_slots_event`
tracepoint (the kernel has Gator support), every atom's start and end per
job slot, frames 360-660 of the intro (bench.sh `GPUTRACE=1`, analysis
`slots.py`/`overlap.py` in the session's scratchpad `msx/`). ms a frame:

| | libmali | malisx2 |
|---|---|---|
| frame | 50.1 | 64.0 |
| slot 1, vertex/tiler/compute, busy | 12.6 | 25.5 |
| slot 0, fragment, busy | 33.5 | 37.1 |
| both slots at once | 6.1 | 1.1 |
| GPU idle | 10.0 | 2.5 |

So malisx2 loses on the vertex side (twice the time), a little on fragments,
and runs the next pass's vertex work beside the current pass's fragments
far less. Its own `MALISX2_MEASURE=timing` (per pass; per draw only on CSF
GPUs) agrees: 240x400 passes, about 20 a frame, 1.45 ms of fragment and
1.34 ms of vertex/tiler each; the 3DS bottom screen and the output to the
panel are under 1 ms a frame together. A capture of its job chains
(`MALISX2_MEASURE=csf`, decoded with `jcdec.py`): per draw a Vertex job
(330 vertices on average, all varyings written to memory: no IDVS on its
Bifrost path), a Tiler job and 2.7 Compute jobs that copy descriptor tables
and uniform words for the draw.

1.05 write-value jobs a draw, each with a barrier, were timestamp queries
from the core: GammaOS's GPU timer in azahar-nano (`vk_gammaos_gpu_timer.h`)
ran in every game. Patch 018 starts it only on request: libmali 50.1 -> 48.7
ms a frame (vertex-slot atoms 86 -> 40 a frame), malisx2 64.0 -> 63.1. Not
the cause of malisx2's missing overlap, then.

What would make malisx2 faster here is in its draw path, upstream's to do:
IDVS on v7, and descriptor tables and uniform words written by the CPU at
record time instead of copy jobs on the GPU.

Seen on the way: the system-sleep hook logs errors on every sleep (`top -p`
with no pid; a GPU devfreq path of the RK3566, `fde60000.gpu`, where the
RK3562's is `ff320000.gpu`). Harmless so far; to fix with the next look at
sleep. Fixed since (19e5bee): `sleep.rk3562` skips the ES CPU check when ES is
not running, and `sleep_governors.sh` uses `ff320000.gpu` on the RK3562.

### malisx2's compiler: LTO and a profile (2026-10-25)

What a game's first start waits for. A fixed workload: Castlevania's boot
preload with Azahar's list of its pipelines kept and malisx2's own pipeline
cache deleted before each boot, so every boot compiles the same 368
("Preload: N pipelines compiled by the driver in X ms", `compbench.sh` in the
session's scratchpad `msx/`). Three boots each, one session:

| malisx2 build | Castlevania, 368 pipelines | Luigi's Mansion, 277 |
|---|---|---|
| -O3 -mcpu=cortex-a53 (as before) | 8.09-8.13 s | 3.13-3.17 s |
| + full LTO | 7.80-7.89 s (-3%) | |
| + LTO and the profile, host build | 7.40-7.41 s (-8.6%) | |
| + LTO and the profile, `build_malisx2.sh` in a chroot | 7.39-7.45 s (-8.6%) | 2.92-2.97 s (-7%) |

-mcpu=cortex-a53 was already there, and with it NEON (always on for aarch64).
The profile: the instrumented driver compiling Super Mario 3D Land's 445
pipelines from an empty cache and Luigi's Mansion's as it starts, then running
(Castlevania, which measures it, left out). Mesa's sources reach the compiler
by absolute paths, which name a profile's file-local functions, so it was made
with the tree at /home/ark/malisx2, the chroot's path; the recipe's build
matching the host's shows it is taken there. Castlevania's speed on malisx2
does not move with it (16.7-16.9 / 15.5-15.8 fps either way; the GPU is the
limit).

Retrained 2026-10-26 on fa1831a (the image's f35bacc, same sources), with
`-fprofile-update=atomic`, as the code of ARMSX2's draw path had changed and a
changed function loses its profile (2026-10-25's left `draw`, `cmd_gfx_fau`,
the push descriptors, `mali_v7_desc_refresh`, `mali_v7_ubo_copy` and
`queue_submit` without it): the same two 3DS games, and ARMSX2 started as ES
does, 75-90 s each of Jak and Daxter (its intro and a save), God of War (a
savestate on the ship) and the Burnout 3 demo, as on the A53 ARMSX2 waits on
the driver's CPU side too. How to make one: malisx2's docs/building.md.
Retrained 2026-10-27 on b74141a (the image's 38e50a5, same sources) after
the CPU-side copies and AFBC for textures: the two 3DS games, God of War on
the ship, Jak and Daxter's intro and Jak and Daxter in game from the owner's
save (no Burnout 3 this time).

perf over the preload: 79% in the driver, 11% libc (malloc/free and their
internals), 7% kernel. In the driver no hot spot: Bifrost register allocation
about 13%, NIR's algebraic passes about 11%, hash sets 6%, then many passes of
1-3% each. A faster allocator could take a few % more; not tried.

## libmali g29p1-5 in place of g29p1-4 (2026-10-25, in 1.3)

Rockchip's libmali mirror (JeffyCN/mirrors) carries g29p1 releases up to -5 on
its `libmali-next` branch (3e59dab3b6, 2026-06-24); the image had -4 from
`BSP/mali.tar.gz`. Both report `g29p1-12eac0` to Vulkan; `rk_so_ver` tells them
apart (4 and 5). Rockchip publishes no change log. Tried on the console bound
over the system blob, the same tests as for a release:

| | g29p1-4 | g29p1-5 |
|---|---|---|
| God of War demo (ARMSX2), fps | 42.2 (earlier run) | 41.2 |
| ND Demo (Dolphin), fps | 44.5 (earlier run) | 46.9 |
| Resident Evil (YabaSanshiro), fps | 60 | 60 |
| AeroGauge (RetroArch, mupen64plus_next), fps (window not noted; not the 54/57 of frames 600-1800) | 59.53 | 59.47 |
| Super Mario 3D Land (Azahar) | 59.8 | compiles its pipelines again on the first start |

ES, its menus and the Vulkan selector work on it. The owner took -5 for the
image (expecting fixes in the shader compiler); `build_deps.sh` fetches it
pinned by commit and sha256. Castlevania's intro on -5: 21.1-21.2 / 20.3-20.4
fps against 21.2 / 20.3 on -4; its first start compiled the 629 pipelines
again in 58 s, the next loaded them in 2.4 s.

Seen on the way, on both: RetroArch with mupen64plus_next does not end on
SIGTERM. `systemctl stop` of the test unit waits its 90 s and kills it, the
picture frozen meanwhile; RetroArch's own quit (the hotkey twice,
`quit_press_twice`) ends it in 1.4 s. Matters if anything stops RetroArch by
signal (a shutdown with a game running): check what ES and the power-off path
send.

Since then (19e5bee): ES's own stop does the same, since systemd sends SIGTERM
to the whole cgroup of `emulationstation.service`, the game included, so a
power-off during an N64 game took a minute and a half. `TimeoutStopSec=10` in
the unit cuts it to 10 s; that is a limit, not a fix. Both observations stand:
the stop of the test unit sent SIGTERM to the whole cgroup (the bash launcher,
n64.sh and RetroArch), and RetroArch did not end; the 0.6 s under "Code from
them, taken" was a SIGTERM to RetroArch alone. Why one ends it and the other
does not is the open question. Cause not found.

## Battery in sleep (2026-10-19)

Sleep is deep now, the black screensaver ends in sleep on battery, two hours
asleep on battery end in a power-off, and the cable no longer wakes the device
for good (KNOWN-ISSUES). Left:

* **~42 mA in deep sleep**, about 4.5 days from full; mostly the board
  itself. GammaOS (Android 14) on the same unit, measured the same way on
  2026-10-19: 37-38 mA in deep sleep, Wi-Fi on or off within 1 mA, and its DT
  has the same regulator states in suspend as ours (on: vcc_ddr, vcc_3v3,
  vcca1v8_pmu, vdda0v9_pmu, vcc3v3_pmu; `rockchip-suspend` disabled in both).
  The 4-5 mA left between the two (about 12 %) is small; if it is worth
  chasing, compare what the drivers leave on: the pwm-fan's `fan-vcc` GPIO
  rail (no state-mem), the stick-RGB LED MCU, Bluetooth (off in the Android
  run), the USB-C controller (husb311).

## Revision A black screen: fixed — decide whether panel-kick can go

What breaks the panel is known (KNOWN-ISSUES): switched off and on again too
quickly, with no delays in its exit sequence, it stays black. Fixed in the
kernel by the stock exit-sequence delays (`23506d78f`) and a 500 ms minimum
DSI off time (`7483a4f83`, from the Android port). panel-kick, which cured
revision A by keeping the display off for about 0.24 s, stays until that is
confirmed. In order:

The fix was confirmed on a revision A unit (v10032026-test, 2026-10-04) and
is in the v09292026 assets. What is left is whether panel-kick is still needed:

1. Test on revision A with `/boot/panel-kick-off`:
   if the picture comes up by itself, panel-kick can go. `link off … waiting`
   in `dmesg` tells whether a fast cycle happened.
2. If it still goes black with `panel-kick-off`, the cycle is not the cause.
   The other candidates: a failed warm takeover of the U-Boot link on the
   first enable, where nothing was switched off before and the off-time wait
   cannot act; or rk915 power-cycling the Wi-Fi rail around the time the
   display first comes up. A `dmesg` from the unit decides between them.
3. Does v09162026 also go black on revision A? Still unasked, and it says
   whether something between the releases introduced it.

## HDMI: find the real connector name, then make detection follow the cable

**Status 2026-10-28.** Steps 1-3 were checked on the device (see KNOWN-ISSUES,
"Measured on the device"): the connector is `HDMI-A-1`, index 1 is right, and
hotplug is now handled by `scripts/rk3562/hdmi-hotplug.sh` + its udev rule. Step 4
stands as advice. What is left is verifying the unplug event on hardware and
deciding what resolution a plugged display should get. Since 2026-10-29 the
handler debounces and defers a plug-in during a game to its end (KNOWN-ISSUES);
to check with a cable: wiggle it in the menu (one restart, not several), plug
it in during a game (the game goes on, HDMI after quitting), pull it during a
game on HDMI (the game ends, the panel comes back lit).

HDMI does nothing on the device. KNOWN-ISSUES has the evidence that the kernel
side matches the Android image where HDMI works, so this is dArkOS-side work.
In order, and the first step is one command:

1. **Ask the card what its connectors are called.** On the device:

       ls /sys/class/drm/
       for c in /sys/class/drm/card0-*/status; do echo "$c $(cat $c)"; done

   HDMI arrives through `route-rgb` and the RK628, so expect something other
   than `HDMI-A-1`. Whatever the name is, it has to replace the hardcoded one
   in `scripts/rk3562/hdmi-test.sh` and in `KCMD_VIDEO` in
   `finishing_touches-rk3562.sh`, which currently says
   `video=HDMI-A-1:1280x720@60`.

2. **Read the connector index rather than assuming it.** The script writes
   `drmConn=1` for HDMI and `0` for the panel. The index EmulationStation and
   RetroArch expect is a position on the card, so derive it from the same
   enumeration as step 1 instead of hardcoding.

3. **Make it run on hotplug, not only at boot.** It is on `@reboot` in crontab
   today. There is already a udev rule firing on DRM events —
   `audio/99-hdmi-audio.rules`, `SUBSYSTEM=="drm"` — which calls
   `audio-switch.sh`. Call the display side from the same place so plugging a
   cable does something.

4. **Drop the `2>/dev/null` while debugging.** It is what turned "that path
   does not exist" into "no display connected", which is why this looked like a
   hardware problem for a while.

Worth knowing before touching the kernel: `rockchip_rgb` binds without the RGB
output if the bridge has not answered by the time initcalls finish, so if the
connector is genuinely absent from `/sys/class/drm`, check `dmesg` for the rgb
and rk628 probe order before concluding anything about the hardware.

## Open questions on the device

* **L2's noise passes the kernel's dead zone** (2026-10-26). With a game
  running and nobody pressing it, ABS_Z rose to 1073 and 1272 a few times in
  30 s (`TRIG_DZ_OUTPUT` 800 in `rk3562-joystick.c`, whose comment has the
  noise peaking at 536); R2 stayed 0. Harmless to programs that read the
  trigger as a trigger, and the GTA San Andreas port, which did not, has
  `trigaxes-off` (KNOWN-ISSUES.md). The vendor's driver starts its 0..9 at
  about 125 mV of a trigger resting near 11 mV. If a game is found to react,
  widen the dead zone (and scale the rest of the travel to the full range),
  after a measurement with the console lying untouched; the right stick's Y
  showed one jump to -4793 in the same run, which also wants that.
* **Is the speaker click gone?** `spk-mute-delay-ms = <100>` is in the device
  tree and the GPIO reads `out lo` in silence, which is necessary but does not
  prove the click went away. Needs an ear.
* **Does a USB mouse do anything?** It enumerates and binds `hid-generic`, and
  an input node appears, but no cursor is visible. EmulationStation may simply
  not use one — that would be expected, not a fault.
* **`OF: graph: no port node found`** in the husb311 connector. Role switching
  works anyway. `live.dtb` from the vendor's EmuELEC (the owner's backup,
  `backup/dtb/live.dtb`, not in this repository) has the complete
  port/endpoint graph if this is ever worth tightening.
* **"Does not turn off the first time"** (a user's report, 2026-10-23): the
  screen goes dark with the lights still on until B is pressed, then the menu
  comes back, several times before it is off. Asked, the user says: 1.2,
  revision A, "shut down" in ES's menu, on battery, a card with dArkOS alone;
  B a few seconds after the screen went dark, and the menu at once, without
  the boot logo. So no reset, and no power-off begun either: once systemd has
  started one, nothing brings ES back. ES asks for it (`sudo shutdown -h now`,
  and `/tmp/es-shutdown` for its launcher) only after its whole exit (saving
  its data, the window closed); a crash on the way has no shutdown, and
  `Restart=on-failure` starts ES again. ES's launcher and service, and ES's
  code for this, are as in 1.2.
  Hence `darkos-poweroff`: ES's `shutdown` event, on "yes", starts it in a
  unit of its own; it waits up to a minute for ES to end, then asks for the
  power-off every second until it happens (attempts in the journal, tag
  `darkos-poweroff`). On the test device: ES killed with SIGSEGV after the
  event, systemd started it again 0.3 s later (the menu would be back), and
  the device was off 3 s after the crash; "shut down" from the menu as
  before, off 5.4 s after "yes". What stops ES on revision A
  is still unknown: the journal of that boot and `~/.emulationstation/
  es_log.txt*` right after a failed try would say.
  Before the user's answers, on the test device (revision B, 1.3), not
  reproduced on battery or on the charger, with the stick LEDs on. The power
  key alone is sleep (ogage runs `pause.sh`), R3 + power is
  `finish.sh`, and a real sleep has the screen and the stick LEDs dark (the
  LEDs 1.3 s after the screen). Checked, all as designed: a press in ES (dark
  in 0.3 s, asleep about 2.8 s later, after the kernel's sync); a press during
  the black screensaver (ES ends it itself); the screensaver's autosleep on
  battery, woken by a press (screen and menu at once); a press on the charger,
  nothing waking it for 45 s, then a press (screen at once). Two things seen
  on the way:
  - A second press in the 2-3 s between the hook's end of `pre` ("asleep")
    and the kernel's freeze (its filesystem sync) is lost: `pause.sh` takes
    it for the press that woke the system, and the device sleeps until the
    next one. A press during `pre` sets the RTC alarm a second on, which
    can fire inside that same sync and be lost as well.
  - Fixed: the power LEDs (gpio 96/97) stayed off after the first sleep after
    boot. The hook exported them and wrote "out" as the direction, which
    drives a pin low, before it saved their state. It reads them first now and
    writes "low" (output, off) after; on the device, the red LED (U-Boot's
    "charging") is lit again after the wake.
  Not yet tried: during a game.

## Decisions nobody has made yet

* **Charging parameters.** The device tree carries the numbers from the
  Android port (4300/300/4654/78/2800); the vendor's own `live.dtb` says
  4280/150/4642/69/3400. The difference that matters is `power_off_thresd`,
  2800 against 3400 mV — the vendor stops much earlier. Deep discharge is
  harder on a cell than a shorter runtime is on a user, so the vendor value is
  probably right, but it is a battery decision and should be made on purpose.
* **Thermal headroom.** Raising `trip-point-1` or `sustainable-power` would buy
  measurable performance and has been deliberately refused; PERFORMANCE.md
  explains why and what to do instead. Open only in the sense that a heatsink
  would change the answer.
* **`CONFIG_ZRAM_WRITEBACK`.** Compiled in, unused. It is the other way to
  spend the eMMC partition — see PERFORMANCE.md.
* **malisx2's FP16 for ARMSX2** (2026-10-29, the owner's call). malisx2
  ce57011 (on 1c45a6e, not yet in the fork) computes fragment shader
  arithmetic in FP16 where nothing needs 32 bits, with `MALISX2_V7_FP16=1`
  in the program's environment (off by default; its first run compiles the
  fragment shaders again). ARMSX2 stays at a7ad453 (2026-10-29), and on it
  (the image's own, 7732b3a, four rounds, two in each order; off / 1 / 3):
  God of War from the owner's save 24.87 / 25.57 / 25.96 fps over 100-1500
  (+2.8%, +4.4%), on the ship 22.62 / 22.71 / 23.13 (+0.4%, +2.3%; over
  800-1500 -1.0% and +1.5%), Jak's intro 35.62 / 35.44 / 35.83 (-0.5%,
  +0.6%). `=3` (both builds compiled, the better kept) is nowhere behind `=1`
  and ahead on God of War; it compiles 78% longer (Castlevania's preload 16.9 s
  against 9.5 s without FP16, 10.3 s with `=1`), and ARMSX2 compiles while a
  game plays, the first time each shader is met. The malisx2 session is looking
  for a count that chooses as `=3` does. The 2.9 numbers that follow came first. Measured here with ARMSX2 2.9 and its 2026-10-28
  profile, as ES starts it, after a warm-up, off against on: God of War on
  the ship 22.48 against 22.80 fps over frames 100-1500 (+1.4%, five rounds,
  on ahead in four), 28.29 against 29.10 over 100-800 (+2.9%, ahead in all
  five), 18.64 against 18.74 over 800-1500; Jak and Daxter's intro 35.98
  against 35.89 (five rounds), Jak in game from the owner's save 16.47
  against 16.41 (two). The pictures are the same. The malisx2 session saw
  +4.1% on God of War with 1.3.1's ARMSX2.
  7732b3a (e09815b on ce57011) picks FP16 by counting the shader's
  operations instead of compiling it twice (Castlevania's preload 10.3 s
  against 16.9; 9.5 without FP16; `MALISX2_V7_FP16=3` is the old choice).
  Measured the same way, four rounds, two in each order: God of War on the
  ship 22.97 against 22.82 over 100-1500 (-0.7%), 28.03 against 29.08 over
  100-800 (+3.7%, ahead in all four) but 19.46 against 18.77 over 800-1500
  (-3.5%, behind in all four); from the owner's save (left by pressing
  Cross 12 s in, as the malisx2 session's drv-ab.sh does) 24.98 against 25.53
  (+2.2%, ahead in all four); Jak's intro 36.01 against 35.63 (-1.1%, behind
  in all four, in both orders). So the count takes FP16 for shaders the two
  compiles kept at 32 bits, and loses where ce57011 did not; told to the
  malisx2 session. To take either: the commit pushed to the fork,
  `MALISX2_COMMIT` moved, and the variable exported for ARMSX2 only (its
  launcher); Azahar and Dolphin are on libmali.
  The fork's main is d398141 since 2026-10-29 (pushed by the malisx2 session
  with the owner's leave): `=1` is the new rule (2c219dc; its four rounds
  against off: God of War +2.0-3.6%, Jak 0, level with `=3`; Castlevania's
  preload 10.5 s), and `MALISX2_V7_AFBC_FMT` is 3 by default (af9083f):
  B8G8R8A8 and RGB565 images are AFBC-compressed (Mario -10% GPU a quad,
  Castlevania +1.5%). Before 1.4 takes it, here: ARMSX2 with `=1` and the
  AFBC default against 1c45a6e, and, the owner's request, PPSSPP and flycast
  on malisx2, which may upload many RGB565 and BGRA8 textures (the RGB565 to
  AFBC copy was checked on Mario only): red and blue swapped, broken
  textures, GPU faults in dmesg. `MALISX2_V7_AFBC_FMT=1` keeps only
  B8G8R8A8 compressed, `0` neither; findings go to the malisx2 session.
  Done 2026-10-29, both built by this recipe (the 1.3.1 rootfs in an
  overlay, the profile applied: .text 6.09 and 6.11 MB), on ARMSX2 1.4's
  build (a7ad453 with patches 0001-0005), two rounds of 75 s, 1c45a6e against
  d398141 with `=1`: God of War on the ship 22.57 / 22.46 against 25.51 /
  23.13 over 100-1500 (+7.8%), from the owner's save 24.90 / 25.17 against
  26.08 / 26.70 (+5.4%); Jak's intro 35.98 / 35.61 against 35.57 / 35.34
  (-0.9%); Jak in game 1248 / 1244 frames in 75 s against 1262 / 1262
  (+1.3%). The pictures are right on both. PPSSPP (Final Fantasy
  Anniversary, Street Fighter Alpha 3 Max), flycast (Crazy Taxi, the 240p
  Test Suite) and Atomiswave (kov7sprt) with DARKOS_VULKAN on each build:
  the same fps (60, kov7 29.2 and 29.9), no crash, the colours right (Crazy
  Taxi's yellow, the 240p menu's blue frame and red text), the textures
  whole, no GPU fault in dmesg (both builds log two or three "Stride 72
  passed to job_submit isn't supported by the kernel" lines at each start).
  The pin and `=1` for ARMSX2 wait for the owner's word.

## Repairs, each its own small job

None open. The three bit-rotted patches are fixed — KNOWN-ISSUES.md says how.

The seven PortMaster compatibility libraries that used to 404 are fixed:
upstream found them on snapshot.debian.org and this fork took the change. All
twenty-one URLs answer now.

Done since this list was written: Yabasanshiro, whose upstream repository is
gone, builds from a pinned commit of a surviving fork; freej2me-plus needed a
java level a current JDK still accepts and a jar name that had been renamed
under it; Kodi's two patch conflicts are resolved and built; Hypseus Singe and
GameTank build since the upstream merge of 2026-10-05 brought their rewritten
patches; and ECWolf builds again (2026-10-15).

## RetroArch fixes after v1.22.2 that need porting by hand

Releases 1.1 to 1.3 carry upstream's fixes that apply as they are
(`retroarch-patch-0021`, `-0022`; 0022 with the zip fix of 1.2). These are in v1.22.2 too, but master
rewrote the code around them, so each needs a small port. Recheck whether a
tag newer than v1.22.2 has appeared first: moving to it would replace all of
this.

* **HTTP client against a hostile server** (66155ff344, eb86df008d). With
  both Content-Length and chunked, the chunk parser reads wild. A huge chunk
  length or a negative Content-Length gets through, and `realloc` results go
  unchecked. The updater talks plain http, so anyone on the network can be
  the server. Port: `response->len = 0` on entering `P_BODY_CHUNKLEN`,
  Content-Length as `size_t` capped at 256 MiB, a capped chunk length, and
  checked reallocs.
* **Cloud sync path traversal** (4ee62a575f, its `cloud_sync` part). A
  hostile server writes outside the sync directory. Matters only if cloud
  sync is turned on.
* **Network commands** (fa49a43dc2). `READ_CORE_RAM` with a large count
  wraps the allocation size, and one UDP packet to port 55355 overflows the
  heap. Off unless `network_cmd_enable`, which Quick Mode turns on; since 0023 the socket listens on 127.0.0.1 only, so it is local. Port:
  cap the byte count at 16 KiB.
* **Saves and config survive a power cut** (250e2d1b48, bb583943fd,
  b867454de3 with d20c1a344e, edb8facc19, 163ef5e698). Write a temporary
  file and rename it, not in place.
* **ALSA can wait forever** (48394abbb2, 6abd13a0d1). Use a finite timeout
  for `snd_pcm_wait`.
* Smaller, clean or nearly so:
  * 9a19e7a94d: netplay client number from the host, one hunk by hand;
  * c7a261ce54: ozone selection past a shrunk playlist;
  * 3eedad0aa5: autosave thread after core deinit;
  * db91cdbf89: gl2.c GLES 3 context for cores;
  * e092c37228, d5e598aedc, f85119508e;
  * 44a041cf96: decompress use-after-free, by hand.

## Worth sending upstream

Six build-system defects were fixed in this fork. Four of them have nothing
to do with this hardware and would help anyone building dArkOS, so they belong
in a pull request to bmdhacks rather than only here:

| | |
|---|---|
| `utils.sh` | `install_package`'s `updateapt` flag is global, so the 32-bit chroot never gets contrib/non-free and never runs `apt update` |
| `setup_partition-rk3562.sh` | the result of `mount` was not checked — a failed mount let the build write past the image for hours |
| `build_retroarch.sh` | `while true` with no attempt counter, which turns one broken patch into a build that spins overnight |
| `utils.sh` | `protect_package` reports `"$${protectedlib} has been marked..."` — `$$` is the shell PID, so every line reads `531{protectedlib}` and never names the package |

The other two — the absolute toolchain path and the 32-bit chroot cloning
upstream's core builds instead of the local fork — only bite in this fork's
layout.

### gl4es: render targets left with a mipmap filter

The report is drafted in `gl4es-fix/UPSTREAM.md`, for an issue at
ptitSeb/gl4es. It is not posted yet: the owner decides when. It covers two
things:
- render-target textures keep `GL_NEAREST_MIPMAP_LINEAR` on the GLES side;
- `SAMPLER[n]` sized by nested macros comes out too small.

Before posting:
- check another gl4es port with the current image, so that the report can say
  whether more than Don't Starve is affected;
- if possible, build gl4es with one of the two suspected spots changed, and
  name the cause instead of a suspicion.

Once gl4es is fixed upstream and ports carry the new build, `gl4es-fix/` and
its line in `portmaster-e2big.sh` can go.

The second one is found (2026-10-25, the owner asked for a fix for every gl4es
port, not only Don't Starve's `postprocess*.ksh`): gl4es's `preproc()` records
`#define N 1` and `#define N 0` from both branches of an `#if defined(...)` it
only writes through, and substitutes the last one in the lines after
(UPSTREAM.md has the details). `gl4es-fix/gl4es-preproc-conditional-defines.patch`
fixes it; `preproc.c` run alone on the game's 104 shaders changes exactly the 8
broken ones, and glslang compiles them. It cannot be fixed under gl4es: the text
`gl4es-fix` gets is already `(2 + 0 + 0)`, with nothing left to tell which
zeros were names, and `preproc` is not exported to interpose. Ports bring
their own gl4es, so: upstream (post the report with the patch when the owner
says), and the image's own fixed gl4es in `/usr/lib/gl4es` with
`libgl_dArkOS.txt` taking it for a port without a gl4es folder; a user deletes
a port's folder to switch (KNOWN-ISSUES.md). Built on 2026-10-25 in the build
chroot (4.5 minutes with the cortex-a53 qemu). Run on the console since
(KNOWN-ISSUES.md, gl4es): Don't Starve with its gl4es folder renamed, the
original `postprocess*.ksh` back and bloom and distortion on (its script
turns them off because those shaders broke). The port takes `/usr/lib/gl4es`,
all 8 postprocess shaders compile and a loaded save looks right.

On the side: libmali's GLSL compiler is closed. malisx2's is ours (Mesa's NIR
and Bifrost back end), but only for Vulkan programs on it; per-program shader
workarounds there would go the way Mesa's driconf does them. GL on malisx2
through zink is out: zink needs far more of Vulkan than malisx2's 1.1 and 24
extensions.

## The dependency stage costs two hours of emulation, not of work

Measured while resuming the 2026-09-29 build, where every package was already
installed and the stage still took the same order of time: about 34 seconds per
entry in `needed_packages.txt`, against 41 seconds when they were being
installed for real. Almost none of that is installing anything.

Each entry of that list runs two separate commands inside the arm64 chroot, and
every one of them is a full process start under qemu:

* `install_package` -> `chroot Arkbuild dpkg -s <pkg>:arm64`, purely a question
* `protect_package` -> `chroot Arkbuild apt-mark manual <pkg>`

`needed_dev_packages.txt` runs at half that, near 17 seconds, because its
`protect_package` call is commented out — which is the measurement that says
where the time goes.

Two ways out, both straightforward:

* Batch. `apt-mark manual` and `dpkg -s` both take any number of package names,
  so the whole list is one chroot invocation instead of 101 or 202.
* Ask on the host. Whether a package is installed is a question about
  `Arkbuild/var/lib/dpkg/status`, readable from outside without emulating
  anything.

Batching is the smaller change and gets nearly all of it, and it is upstream's
code rather than ours.

The saving is twice what the paragraph above assumed, because the lists are
walked a second time. After `cleanup_filesystem.sh` purges the build
dependencies it reinstalls what the finished system needs by reading the same
files again: `needed_packages32.txt` (27), `needed_packages.txt` (101) and
`bluetooth_needed_packages.txt` (17) — 145 entries, each with the same
`dpkg -s` plus `apt-mark manual` pair inside the chroot. Measured on the
2026-10-01 build, that one step ran from 00:12 to roughly 02:00 with nothing
to install: every package was already present. `kodi_needed_dev_packages.txt`
is skipped there, but only by accident of a chipset test that reads `*3566*`
and so never matches this board.

So the whole build pays the per-package emulation cost twice, in `build_deps.sh`
and again in `cleanup_filesystem.sh`. Batching both would return roughly two
and a half hours per build.

Since then the 64-bit `build_deps.sh` comes from a copy when nothing it is made
of has changed (BUILDING.md, "The chroot from the cache"), so such a build pays
the cost in `cleanup_filesystem.sh` (2 h 6 min in the 2026-10-19 build) and in
the 32-bit chroot's `build_deps.sh 32` only ("Build: what still runs under
qemu", above). Batching those two is still open.
