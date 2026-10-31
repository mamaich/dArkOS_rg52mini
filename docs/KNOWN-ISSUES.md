# Known issues

All of these are visible in the build log. `make rg52mini` ends with an audit
that names them, so start there rather than reading 70 000 lines. Build
20261025224049 (release 1.3), the same as 1.2's:

    ==================== COMPONENT AUDIT ====================
      ---
      with binaries: 37   empty: 0   no executable: 0

      Build steps that gave up (from build.log):
        none

      Packages that failed to install:
        Could not install needed library libfuse2.
        Could not install needed library libpcap0.8.

      Cached failures (an empty tarball under a key that still matches,
      so every later build restores the failure instead of retrying):
        none
    ========================================================

`libfuse2` and `libpcap0.8` do not exist in trixie under those names.

## Components that did not build

Three failed the same way: a patch in `rk3562_core_builds/patches/` no longer
applied to its upstream, `builds-alt.sh` stopped, and the copy that followed
found nothing. All three build now.

| | patch that stopped applying | fixed |
|---|---|---|
| Hypseus Singe | `hypseussinge-patch-0001-buildfix.patch` | upstream rewrote it (2026-07-10); here since the merge of 2026-10-05 |
| GameTank | `gametank-patch-001-disable-joystick.patch` | upstream rewrote it (2026-07-11); same merge |
| ECWolf | `ecwolf-patch-002-add-exit-menu.patch` | here, 2026-10-15 — below |

**ECWolf: upstream took the patch in.** ECWolf's own 1bff92d (2026-02-23, "add:
ESC key as button to return to main menu") adds the same one-line
`controlScheme[]` entry the patch added, so the patch could not apply a second
time. It is deleted. Two things follow from it:

- Upstream calls the button "Main Menu" where the patch said "Escape Game", and
  ECWolf names its config keys after the button (`Joystick_%s`, spaces to
  underscores, `c_cvars.cpp`). `ecwolf.cfg.rg52mini` now binds
  `Joystick_Main_Menu = 4`, which is Select. Under the old key name the button
  would be left unbound — read from the code, not tried.
- `scripts/ecwolfsa.sh` applies the patches only when `find *.patch` finds one,
  and `ecwolf-patch-001-add-video-patch` (window modes 480×320, 854×480,
  960×544) had been applied only because 002 matched. It is renamed
  `ecwolf-patch-001-add-video.patch`.

Two more things kept ECWolf missing once the patch broke, and are fixed in
`build_ecwolfsa.sh`. The cache key came from the bitbucket mirror, which stopped
at 51994be while the build clones GitHub, so the key never matched and every
build would have rebuilt. And a failed build still wrote an empty tarball. The
key is now `git ls-remote` of the GitHub repository; the cache is taken only
when it holds `opt/ecwolf/ecwolf` and written only after a build that made it.

Checked on 2026-10-15:

- `builds-alt.sh ecwolfsa` in a trixie arm64 chroot under qemu took 22 minutes.
  It used the system SDL2 and SDL2_mixer and linked in its vendored SDL2_net,
  since trixie's runtime package has no `.so` to link.
- `build_ecwolfsa.sh`, run with the chroot stubbed out, threw away the 127-byte
  cached failure, wrote no cache after a failed build, wrote one after a good
  build, and took it on the next run.
- On the device, started through `ecwolf.sh standalone` with the Wolf3D 1.4
  shareware data from maniacsvault.net:
  - SDL 2.32.10, the game controller `rk3562-joystick`, audio at 44100 Hz;
  - landscape and upright on the panel, checked with the camera;
  - Select in a game opens the main menu, and the config ECWolf writes back on
    exit keeps `Joystick_Main_Menu = 4`;
  - both Quit and Select+Start end the game.

Yabasanshiro was a fourth, for a different reason, and is fixed too — see
below.

**Yabasanshiro was different, and is now fixed.** `scripts/yabasanshirosa.sh`
cloned `https://github.com/devmiyax/yabause` at tag `pi4-1-9-0`, and that
repository no longer exists — git asks for a username and the build reports it
as a network problem. No surviving mirror carries the tag:

    devmiyax/yabause                            gone
    sydarn/yabasanshiro                         no tags at all
    Mechafatnick/YabaSanshiroPi                 no tags at all
    pirrypirrypirry/yabasanshiro-pirry-release  four tags, not this one
    gfhhhg/lr-yabasanshiro                      no such tag
    libretro/yabause                            a different project

It is now built from `Mechafatnick/YabaSanshiroPi`, pinned to commit
`91990f8` (2021-09-04, roughly the 1.9.0 era) rather than to master, so it stays
reproducible. Of the eleven patches it inherits five apply (03-06,
and 07 after the first build pass) and six are skipped for reasons rather
than convenience, listed in the recipe; 12 and 13 are ours. `YAB_WANT_VULKAN=OFF`,
because that fork has no `yabause/src/vulkan` at all — not because the device
lacks Vulkan, which it has. The `n2.cmake` toolchain file stays: it is the
aarch64 one, and `pi4.cmake` would build for armv7.

It compiles, first try, with no errors on the step: a 3.9 MB stripped aarch64
binary. It did not run, tried first on 2026-10-24 with Resident Evil: a
SIGSEGV about 10 s in, with and without the BIOS. The SH-2 dynarec's arm64
templates kept a register number in w4 across calls to the memory handlers,
which may change it, and GCC 12's handlers do (patch 13, the same as
devMiyax's 57af8f86 upstream, which this fork predates, plus three templates
upstream missed). The sound thread also polled a core flat out, its timed wait
given `ctime()`'s string as a deadline (patch 12). With both the game runs at
59.9 fps and the process takes 105% of a core instead of 190%.

The libretro core (RetroArch, `yabasanshiro`) is a different build and ran
the game without either: 57.8 fps, about 150% of a core.

Building it again needs the image build's GCC 12 (`build_deps.sh`): libchdr,
which CMake clones unpinned from devmiyax/libchdr (unchanged since 2020),
builds its FLAC with `-std=c99 -D_POSIX_C_SOURCE=199309L`, and GCC 14 makes
the implicit `strdup`/`fseeko` there errors.

## Kodi builds, and costs two days doing it

It did not build in the 2026-09-15 image:

    DRMAtomic.cpp  Hunk #1 FAILED
    There was an issue applying kodi-patch-001-rk3562-rga-rotation.patch
    tar: Arkbuild/opt/kodi: Cannot stat: No such file or directory

Kodi itself is pinned at 21.3-Omega, so that was never the problem.
`kodi-install` is cloned at master, and it had grown two patches landing on the
same files as ours: `0017-kodi-patch-mali-egl-display` is literally our own EGL
fix, which upstream adopted, and `0016-miniloong-internal-only-rotate270`
rotates another device the other way through the same DRM paths our 90-degree
RGA rotation uses.

`build_kodi.sh` removes both before staging ours, and with that every patch
applies without a conflict. **The 2026-09-16 image carries Kodi 21.3-Omega,
266 MB in `/opt/kodi`, with 67 addons, and it runs on the device.**

Be ready for what it costs: about 32 of that build's 43 hours. Very little of
that is compiling Kodi — most of it is one full cmake configure per addon,
seventy-one of them, single-threaded under qemu, at ten to twenty-five minutes
each. The result caches as `kodi_rg52mini.tar.gz`, 105 MB, so a later build
unpacks it in a minute. That makes the cache worth guarding, which is what the
guard described below is for.

Kodi has a second, worse habit. Its dependency resolution removed the SDL2
`-dev` packages and `libasound2-dev` along with them, which silently broke
bluez-alsa later in the same run:

    REMOVING:
      libasound2-dev  libsdl2-gfx-dev    libsdl2-mixer-dev
      libsdl2-dev     libsdl2-image-dev  libsdl2-ttf-dev

    configure: error: Package requirements (alsa >= 1.0.27) were not met

`build_bluealsa.sh` now names `libasound2-dev` itself instead of inheriting it,
and `finishing_touches-rk3562.sh` only enables `bluealsa.service` when the
binary exists — enabling a unit whose binary is missing is how a build-time
failure became "Failed to start bluealsa.service" on every boot with no clue
why.

## PortMaster compatibility libraries — fixed upstream

Seven of the twenty-one URLs in `fetch_compat_libs.sh` used to be dead, all of
them pinning a bullseye version on `security.debian.org`, which drops
superseded packages as a release ages: libavcodec58, libavformat58, libavutil56,
libswresample3, libswscale5, libvpx6, libaom0.

**This page used to state that those versions were gone from every archive and
that no URL existed to switch to. That was wrong.** They are on
`snapshot.debian.org` — the ffmpeg ones and libvpx6 and libaom0 under
`archive/debian-security/<timestamp>/`, libjpeg8 under
`archive/debian-ports/`. The mistake was looking in `archive.debian.org` and in
snapshots of the main archive, and concluding from their absence there that
they were unobtainable.

Found and fixed by christianhaitian upstream (`1dac9ef`, reported by
@Sundownersport), taken into this fork with one conflict — our copy still had
the old libvpx6 URL, since the intermediate upstream commit that moved it was
never pulled. All twenty-one now answer 200 and serve a real `.deb`; checked,
not assumed.

The build still treats a failed fetch as a warning rather than a fatal error,
which is our change and independent of this: a dead mirror should not take down
a run that is nine hours in. Upstream reached the same conclusion separately in
`ffcfced`.

One library still does not arrive: the 32-bit `libjpeg.so.8`. The script
makes each armhf URL from the arm64 one by replacing `_arm64` with `_armhf`,
and `debian-ports/.../pool-arm64/` holds arm64 packages only, so that URL
answers 404 (`[WARNING] could not fetch libjpeg.so.8 (armhf)` in the 1.3 build
and in every earlier full build log kept here). The image has the 64-bit one
only; a 32-bit port that needs `libjpeg.so.8` will not find it. No such port is
known so far.

## PortMaster ports failed with "Argument list too long" — fixed

Reported by users: every PortMaster port failed at launch with "Argument list
too long" (reported as "command line too long"). Reproduced on 2026-10-05,
first in WSL and then on the device, with PortMaster 2026.10.03 and a minimal
port built from PortMaster's standard header.

`get_controls()` in PortMaster's `control.txt` recognises a device by its
joystick node in `/dev/input/by-path/` (`odroidgo2-joypad`, `singleadc-joypad`
and a few others). Ours is `play_joystick`, which it does not know, so `DEVICE`
stays empty and the whole `gamecontrollerdb.txt` — 472 KB — ends up in
`sdl_controllerconfig`. Every port then does

    export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"

and a single environment string may not exceed `MAX_ARG_STRLEN`, 131072 bytes.
From that line on, every `execve()` in the port script fails with `E2BIG`. The
other dArkOS devices are not affected: their joypad drivers are on the list.

PortMaster already carries a guard for exactly this in `mod_EmuELEC.txt`: wrap
`get_controls` and drop `sdl_controllerconfig` when it is over 100000 bytes.
SDL still gets the mappings, including the entry `mapper.py` appends for our
joystick from `~/.config/emulationstation/es_input.cfg`, through
`SDL_GAMECONTROLLERCONFIG_FILE=/tmp/gamecontrollerdb.txt`.

**Fix:** `scripts/rk3562/portmaster-e2big.sh` appends the same guard to
`mod_dArkOS.txt`, which ports source after `control.txt`. PortMaster is
installed by the user and rewrites its own files on every update, so the guard
cannot simply be shipped: `portmaster-e2big.service` adds it at boot and
`portmaster-e2big.path` adds it again whenever `mod_dArkOS.txt` changes. Tested
on the device: the test port fails without the guard and runs with it; after
the file is overwritten, as a PortMaster update does, the path unit restores
the guard within a few seconds; after a reboot with the file restored, the
service adds it again (`/roms` is mounted by then).

The limit of the fix: SDL reads `SDL_GAMECONTROLLERCONFIG_FILE` from 2.0.22 on.
A port that bundles an older SDL gets no mapping for the built-in pad and sees
it as a plain joystick. The complete fix belongs in PortMaster: a
`get_controls()` branch for `platform-play_joystick-event-joystick` with
`DEVICE=1900a4dd726b333536322d6a6f797300` cuts the string to 356 bytes and works
with any SDL. On the device the node is indeed
`/dev/input/by-path/platform-play_joystick-event-joystick`.

A side effect seen during the test, not caused by the fix: on the first port
run `mod_dArkOS.txt` re-points `libEGL`, `libGLESv2`, `libgbm` and `libmali` at
`libMali.so`. They already point there in the image, so nothing changes.

## v10052026: the power button rebooted revision A boards — fixed in the kernel

The bootloader v10052026 ships (`uboot-oc-stocktee-shmkept.img`) has the BL31
with the overclocking frequency table, built by `tools/rg52mini/bl31_oc.py`
in the U-Boot fork with `--drop-lowest gpu`. BL31 accepts a clock rate only if
it is in its SCMI list, and that option drops the GPU's 200 MHz step to make
room for 1000 MHz: the GPU list is 300…1000 (checked in the image: the list
`300, 400, … 900, 1000+63` is in the FIT, the stock `200 … 900+63` is not).

The Mali driver parks the GPU at `POWER_DOWN_FREQ` before every runtime
power-off, and that was 200 MHz
(`drivers/gpu/arm/bifrost/platform/rk/mali_kbase_config_rk.c`). The call
failed every time — `mali ff320000.gpu: failed to set power down rate`, 34 times
in one boot on GammaOS — and the GPU powered off at the rate it was running,
up to 900–1000 MHz. On revision A (RK915) that made the power button reboot
the device in any mode; revision B only logs the error. Found and confirmed on
revision A in the GammaOS port (its kernel commit 225c0ee0e).

**Fix:** `POWER_DOWN_FREQ` is 300 MHz (`kernel_rk3562` fa0452d3b), the lowest
step in both the stock and the overclocking list. Check after a build:
`dmesg | grep -c "failed to set power down rate"` gives 0, and at idle
`grep scmi_clk_gpu /sys/kernel/debug/clk/clk_summary` shows 300000000.

The rest of the table was checked against this kernel: the CPU list loses
1896 MHz for 2208, and the kernel has no 1896 OPP (1800, then 2016); the GPU
OPP table starts at 300; the NPU list is untouched. `drivers/rknpu/` also parks
at 200 MHz — fine with this BL31, but needs the same check if a future table
drops the NPU's lowest step.

## Standalone DuckStation: not in the image since 1.4

DuckStation is CC BY-NC-ND 4.0. Its README allows "unmodified releases and
code" to be passed on and says "pre-configured settings and packages are
considered modifications"; newer READMEs also forbid publishing modified
versions, patches included. Up to 1.3.1 the image carried upstream's AppImage
(0.1-10495, the "mini" frontend, the last that runs without a display server)
unmodified, as `/opt/duckstation/duckstationsa`, with our settings.ini and
controller DB in DuckStation's folder. On 2026-10-28 master moved those out,
for the launcher to make on the first start, and the owner wrote to
DuckStation's author. The author did not agree to that, and does not publish
the mini frontend himself; newer DuckStation (0.1-12083) is Qt only and needs
Wayland or X. Standalone DuckStation also brought no speed the image would
miss (TODO.md, "The PlayStation emulators compared"). So on 2026-10-30 the
owner took it out of the image and of the repositories: the AppImage,
`build_duckstationsa.sh`, its settings, the `standalone-duckstation` launcher
and its entry in ES, and rk3566_core_builds' recipe for the old source build
with its patches.

PS1 games run in RetroArch, as ES starts them by default: pcsx_rearmed
(RetroArch32), or the duckstation and swanstation cores.

An update to 1.4 deletes the files of it the old image had and the user did
not change: the program, its launcher, an untouched settings.ini.
`ota/hooks/1.4-duckstation-removed.sh` takes `standalone-duckstation` out of
an es_systems.cfg that is the user's own copy, and out of the games ES was
told to start with it (their gamelist.xml), which then start with the
system's emulator; a system set to it needs nothing, as ES takes the
system's first emulator for a name it does not list. What stays: the memory
cards (per game in `/roms/psx`, `<game>_1.mcd`), the savestates in
`/roms/psx/savestates`, the per-game settings in
`/roms/psx/duckstation_gamesettings`, and a settings.ini the user changed.

## Doom: the player turned by himself — fixed in 1.4

In LZDoom (ES's default for Doom) and GZDoom on the RG52 Mini the player
turned all the time, without a key pressed. Both inis came from the rk3566
one, whose pad has its right stick on axes 2 and 3; the rk3562-joystick has
L2 on axis 2 (an axis that rests at one end of its range), the right stick on
3 and 4 and R2 on 5, so `Axis2map=0` turned the player at full tilt and the
right stick's X looked up and down. The buttons were the rk3566's too: D-pad
up opened the main menu, Home (ogage's) and L3 (which closes by itself here)
changed weapon, and Start, Select and B did nothing. Found with Freedoom on
the console.

`gzdoom/scripts/doompad.py` puts the RG52's layout into an ini: L2 and R2
drive nothing, the right stick turns and looks (the old right stick's scale
and dead zone), the main menu on Start, the automap also on Select, and in
Doom B jumps, D-pad up and down change weapon, D-pad right crouches, nothing
on Home or L3. The RG52 Mini's and RG43H's inis and their backups (which the
Restore Default tools now pick for these consoles; they took the RG503's
before) are made with it, and doom.sh runs it once on a user's inis, which
the engines write back on every exit; it only moves an ini still on the
rk3566 layout. Checked on the console with Freedoom in both engines: the
player stands still, D-pad up changes weapon, Start opens the menu.

## ScummVM opened every game on a dialog — fixed in 1.4

Up to 1.3.1 every ScummVM game started on "The preferred audio device
'fluidsynth' was not found (...) Attempting to fall back to the next available
device", a dialog the pad does not close (Enter on a keyboard does). Our
scummvm.ini sends MIDI to FluidSynth, but ScummVM was built without it: the
recipe never installed FluidSynth's headers (gzdoom.sh lists them, and a
cached GZDoom never runs), and the soundfont the config names, RetroArch's
scummvm `extra/Roland_SC-55.sf2`, is not in this image either. 1.4 builds
ScummVM with FluidSynth (libjack-jackd2-dev first: libfluidsynth-dev alone
pulls JACK 1 and apt swaps out the image's libjack-jackd2-0), the configs of
the RG52 Mini and the RG43H name Debian's `/usr/share/sounds/sf2/FluidR3_GM.sf2`
(fluid-soundfont-gm is installed), and `scummvm.sh` puts that one into a
user's config that names a soundfont that is not there. Day of the Tentacle
starts straight into its intro, the music through FluidSynth (about half a
core of the A53).

## XRoar and openMSX did not start — fixed in 1.4

Both link `libGL.so.1`: their OpenGL output is built because the chroot has
GL headers, and neither configure can be told to leave it out. The image has
no desktop GL (only Mali's GLES), so they ended at once with "libGL.so.1:
cannot open shared object file", in 1.3.1 and before. Their launchers
(`coco.sh`, `openmsx`) now put `/usr/lib/gl4es` first in the library path, so
gl4es stands in; XRoar shows the Dragon 32's BASIC prompt, openMSX C-BIOS.

On the 1.4 image (2026-10-31) only half of that held. openMSX runs a
cartridge from C-BIOS. XRoar started and loaded its ROMs, but as a Dragon 32
and as a CoCo 2 its screen stayed on a field of `@` and never got to BASIC.
Not the GPU (SDL's software renderer showed the same), not the sound
(`-ao null` too), not DragonDOS (`-no-machine-cart` too), and not the build:
a CPU trace on the console matched one of XRoar 1.12.1 built on the PC, both
in BASIC's keyboard loop after 2 s. The launchers' `-ram 2048` did it. That
is 2 MB, for a CoCo 3; XRoar 1.12 gives it to a Dragon or a CoCo 1/2 too,
and their screens stay blank. Without it the Dragon 32 shows its BASIC (and
DragonDOS 1.0), the CoCo 2 Extended Color BASIC 1.1.

The Dragon 32 system had an older fault besides: its command starts
`/usr/local/bin/dragon.sh`, which is `xroar/dragon.sh`, but build_xroar.sh
installed only `coco.sh`, so its games never started. For 1.5 coco.sh passes
`-ram 2048` to a CoCo 3 only, and dragon.sh is installed, without it and with
gl4es's libGL; both were checked on the console as ES starts them.
Nothing else in /opt or /usr/local misses a library, apart from Kodi's
visualisation add-ons, which want desktop GL and are not for this build.

openMSX copies its data (Tcl scripts, machines, extensions) to
`/roms/bios/openmsx` on its first start and reads it from there. A card that
ran 17.0 would keep 17.0's scripts under 19.1, so build_openmsxsa.sh stamps
the data with the recipe's cache key and the launcher puts a newer copy over
an older one, keeping the user's `settings.xml` and system ROMs.

## Dolphin: a held hotkey moved the stick's centre, no Sys folder — fixed in 1.4

* The hotkey patches gave the Hotkey button bit 0x2000 of the GameCube pad
  word, which is PAD_GET_ORIGIN: while it was held, the SI device took the
  sticks' position as their origin and the game was told to re-read it, so
  pressing the hotkey with a stick pushed could leave the stick drifting.
  2609a's patch 008 moves the bit into a field of its own.
* The package had Dolphin's binary only; `Data/Sys` was copied into the user
  folder, where it counts as the user's own overrides, and the places Dolphin
  reads its own data from were empty: no `codehandler.bin` (Gecko codes) and
  no IPL fonts. 1.4 installs it as `/opt/dolphin/Sys`; an update deletes the
  user-folder copies the user did not change.

## DSperate stopped at start on an ext4 /roms — fixed in 1.3.1

On the dual-boot card, whose /roms is GammaOS's ext4 storage, every game in
DSperate ended a moment after its window opened: "terminate called after
throwing an instance of 'std::bad_alloc'". nds.sh sets `cheats =
/roms/nds/cheats`, a directory; with no usrcheat.dat in it, DSperate (e04b806)
kept the directory as the database's path and read it as a file. fopen takes
a directory, and on ext4 a directory's end is INT64_MAX (the htree EOF), so
`ftell` gave 2^63-1 and the vector resize asked for that much. On exFAT a
directory reports a small size and the read just fails, which is why the
EASYROMS card never showed it. Found with an LD_PRELOAD hook printing a
backtrace at an `operator new` of more than 1 GB.

`dsperate-patch-0001-cheats-directory` (rk3562_core_builds 6978c9e): a
directory without usrcheat.dat means no database, and the reader takes
regular files only. The recipe also pins DSperate at e04b806 now (it built
main's head of the day), and its patches had never applied: they were copied
into `dsperate/` while the clone is `DSperate/`. Checked on the device:
Diddy Kong Racing DS and the Pokemon demo at 60 fps, START and A work.

Since then the screens are side by side by default (`layout = horizontal` in
the shipped ini; nds.sh adds the line to an ini that has no layout line). A
layout picked with the hotkey is still kept per game in games/<rom>.ini.

## DSperate (NDS) is slow unless the governor is performance — fixed in 1.3

`perfmax` now sets `performance` when no governor was chosen ("Games ran on the
ondemand governors", below), so the workaround below is what every game gets
by default.

DSperate, the second NDS emulator since v10062026, keeps about one CPU core
busy. Under the default `ondemand` governor that reads as light load, and the
CPU spends a third of its time at 1.4 GHz or below: the Pokemon Diamond &
Pearl demo runs at 41-47 fps with crackling sound, the 3D field of the Dragon
Quest Monsters Joker 2 demo at 48. Under `performance` (CPU 2016 MHz, GPU
900 MHz) both run at 59-60 fps with clean sound. Measured on the device on
2026-10-07; the figures are in TODO.

**Workaround, up to 1.2:** in EmulationStation set the CPU governor of the NDS
system, or of the game, to `performance` when using DSperate. DraStic, the
default NDS emulator, is lighter and does not need it. The image was left as
it was then (2026-10-07); 1.3 needs no workaround.

The image's DSperate is built with Vulkan and draws 3D on the GPU
(`gpu3d = true` in its default config); that does not replace the governor
setting.

## Vulkan programs on the portrait panel: VK_LAYER_DARKOS_rotate (after v10072026)

**The problem (up to v10072026).** Vulkan programs that present through
`VK_KHR_display` drew a stretched, sideways picture. That covers RetroArch on the
Vulkan driver, and PortMaster ports on SDL's KMSDRM Vulkan path, Mina the
Hollower for one. The vendor's firmware does the same.
- GL programs get a landscape picture: SDL and RetroArch's `drm_go2` context turn
  every frame onto the 720x1280 panel with the RGA.
- `VK_KHR_display` hands a Vulkan program the panel as it is: display, mode,
  surface and swapchain are all 720x1280.
- The VOP2 planes cannot turn by 90 degrees, and RetroArch's
  `screen_orientation` made no difference.

In RetroArch it seemed tied to "Bilinear Filtering" only because changing the
filter restarts the video driver, which is when a menu switch to Vulkan takes
effect.

**The fix.** `vk-rotate-layer/` holds an implicit Vulkan layer. It is built by
`build_vkrotate.sh`, installed as `/usr/lib/aarch64-linux-gnu/libVkLayer_DARKOS_rotate.so`
with `/usr/share/vulkan/implicit_layer.d/VkLayer_DARKOS_rotate.json`, and the
Khronos loader loads it into every Vulkan program.
1. **Landscape to the program.** For a display whose physical resolution is
   portrait, the layer reports width and height swapped. That covers display
   properties, display modes (their handles are remembered), display plane
   capabilities on those modes, and the capabilities of a surface made on them.
2. **Portrait underneath.** A display mode or display plane surface asked for
   with a landscape extent is created portrait underneath (SDL asks for one the
   size of its 1280x720 window).
3. **The swapchain.** A swapchain on such a surface is created 720x1280. The
   program gets landscape images of the layer's own, with its usage plus
   sampling.
4. **The turn.** In `vkQueuePresentKHR` the layer submits a prerecorded pass
   that waits on the program's semaphores. A full-screen triangle samples the
   program's image turned by 90 degrees into the swapchain image (nearest, 1:1).
   The program's image goes back to PRESENT_SRC, and the real present waits on
   the pass.
5. **Landscape displays are left alone**, so the layer does nothing on other
   devices.
6. **Scaled modes** (after v1.0), as `sdl2-patch-0008` gives GL.
   - The Mali driver makes no display mode but the panel's own:
     `vkCreateDisplayModeKHR` for 640x480 fails with
     VK_ERROR_INITIALIZATION_FAILED.
   - SDL's KMSDRM Vulkan path wants a mode exactly the size of the window. So
     standalone flycast, whose shipped window was 640x480, could not start
     Vulkan and fell back to GL.
   - The layer lists 960x540, 854x480, 800x600, 640x480 and 640x360 after the
     panel's own mode, so a program that takes the first mode or the biggest
     one still gets 1280x720.
   - It also accepts `vkCreateDisplayModeKHR` for any size up to 1280x720 when
     the driver refuses.
   - These modes are the layer's own handles, which never reach the driver. A
     surface on one is the panel's whole mode underneath, and its capabilities
     report the scaled size.
   - The same pass scales as it turns: bilinear, with the aspect ratio kept and
     centred, so 4:3 gets black bars. 854x480 fills the panel, as in SDL.
7. **The panel as it is, for a program that turns the picture itself** (after
   v1.0).
   - Standalone DuckStation (in the image up to 1.3.1) is an AppImage with its
     own SDL3, which sees the panel as 720x1280; it turns its picture itself
     (`Rotation = Rotate90`).
   - With the layer from v10082026 (1.0) on, it found no 720x1280 mode, and
     the driver makes none, not even of the panel's size. So it stopped with
     "SDL_Vulkan_CreateSurface() failed: Vulkan couldn't find a predefined mode
     for that window size".
   - Now a portrait `vkCreateDisplayModeKHR` of the panel's size gets a handle
     of the layer's own for the panel's mode. The plane capabilities and the
     surface on it are the driver's, nothing turned, as before the layer.

Controls:
- `DARKOS_VK_ROTATE_DISABLE=1` turns the layer off (the loader reads it from the
  manifest);
- `DARKOS_VK_ROTATE=270` turns the other way;
- `DARKOS_VK_SCALED_MODES=0` leaves the scaled modes out;
- `DARKOS_VK_ROTATE_DEBUG=1` logs to stderr.

Tested on the device:
- **RetroArch on Vulkan** (Genesis Plus GX): "[Vulkan] Using resolution
  1280x720". The layer logs "swapchain: program 1280x720, panel 720x1280, 3
  images", and the picture is right way up and full screen.
- **Mina the Hollower**: its log shows display, mode, surface and swapchain at
  1280x720 (720x1280 before), and the picture is right way up.
- **DSperate** (Vulkan 3D, presenting through SDL's GL path): unchanged.
- **Scaled modes**, checked on the device:
  - `vk-rotate-layer/test/vkmodetest.c` lists 1280x720 first and then the five
    scaled modes. Asked for one mode only, it gets 1280x720 with VK_INCOMPLETE.
  - Its test picture on 800x600 (listed) and on 1024x600 (created) comes out
    right way up. The corners are in place, the yellow frame on the image's
    edge is whole, and the bars are black. 1280x720 is still 1:1.
  - Standalone flycast with a 640x480 window: "display surface 640x480 (panel
    720x1280, scaled)", Vulkan, with the picture 960x720 between bars.
  - Mina the Hollower still takes mode[0] = 1280x720.
- **The emulators on Vulkan**, checked on the device (2026-10-12):
  - Standalone **PPSSPP** 1.20.4 (Vulkan by default): it takes the 1280x720
    mode, and `ppsspp-patch-010` sees a landscape swapchain and does not turn,
    so the picture is right way up.
  - **Dolphin** (GameCube demo): it takes the 1280x720 mode, and
    `dolphinsa-patch-011` sees a landscape swapchain and does not turn, so the
    picture is right way up. 2609a (1.4) has no rotation patch at all.
  - **PCSX2** (Vulkan by default) turned the picture by itself
    (`DisplayRotation = 90`), so with the layer it lay on its side. The
    rg52mini template now has 0, and `standalone-pcsx2sa` changes 90 to 0 in
    an ini already on /roms.
  - Standalone **DuckStation**: it failed before item 7; then its picture was
    right way up (it is not in the image since 1.4).
  - **PPSSPP-2021** has no Vulkan on KMSDRM ("Vulkan subsystem 13 not
    supported", then exit). Its rg52mini config asked for Vulkan, so it never
    started. The configs now ask for GL, and `ppsspp.sh` changes an old one.
    On GL its picture is right way up.
  - **RetroArch** with `video_driver = "vulkan"` and the SwanStation core: the
    picture is right way up.
  - Standalone **flycast**: see `build_flycastsa.sh`.

Not done: a program that presents several swapchains in one call gets only
the first one turned.

**No Vulkan for 32-bit programs.** On this device only 64-bit programs can use
Vulkan, so the layer is built for aarch64 only.
- **The Mali driver.** Rockchip ships Vulkan for the Mali-G52 only in its 64-bit
  libmali. None of the armhf builds exports `vk_icdGetInstanceProcAddr`: g13p0,
  which the image uses because the 32-bit g29p1 crashes in GL setup
  (build_deps.sh); g24p0, the newest armhf build in Rockchip's libmali mirror
  (JeffyCN/mirrors); and g2p0 in `BSP/mali32`. So the armhf Vulkan loader has no
  driver.
- **The manifest.** `rk_vk.json` names an aarch64 library by absolute path,
  which fits: `libvulkan_darkos.so`, which loads the aarch64 libmali or
  malisx2 (next section).
- **Mesa.** panvk drives this GPU only on the panfrost kernel driver, not on
  Rockchip's Mali kbase.
- **RetroArch.** `retroarch32` is built without a Vulkan driver: with
  `video_driver = "vulkan"` it logs "Couldn't find any video driver named
  \"vulkan\"", and its menu does not offer one. Cores that want Vulkan run in
  the 64-bit `retroarch`, which the layer turns.

**Mina the Hollower, port side.** The port's launcher sets
`GOTHIC_BACKEND=gles` for `DEVICE_NAME = RG52MINI`, so that the game uses GLES,
which SDL turns. But it starts the game with `$ESUDO env ...`. On dArkOS
`$ESUDO` is `sudo`, which drops exported variables, so the game ran on Vulkan
anyway. It now presents right through the layer.

## Vulkan: Arm's driver or malisx2, chosen per system (1.3)

Two Vulkan drivers can run the Mali-G52: Arm's own, in libmali, and malisx2
(github.com/bmdhacks/malisx2), Mesa's shader compilers and Vulkan runtime over
Arm's kernel driver, written for the ARMSX2 PS2 emulator; GammaOS ships it for
azahar-nano on the G52. Measured here (TODO.md, "malisx2"): it compiles
pipelines about six times faster (a game's first run stutters less), but draws
Castlevania: Lords of Shadow – Mirror of Fate a quarter slower and God of War 5%
slower, implements Vulkan 1.1 with 24 device extensions against libmali's 1.3
with 135, and in 1.3 Dolphin did not start on it (it does since 1.3.1, below). So
libmali stays the default, except for ARMSX2 since 1.4: malisx2 1c45a6e (IDVS,
index ranges on the GPU) draws God of War and Jak and Daxter in game 10-12%
faster than libmali there, its pictures the same, and vulkan.conf names it for
`armsx2-sdl` (an OTA hook adds the line to a user's own copy that names no
driver for it; MALI in a PS2 game's EMULATOR SETTINGS still takes Arm's). 1.4
ships d398141, and ARMSX2's launcher sets `MALISX2_V7_FP16=1` (fragment shader
arithmetic in FP16 where nothing needs 32 bits): God of War another 5-8%, Jak
level (TODO.md, "malisx2 for 1.4").
Azahar and Dolphin stay on libmali: Castlevania 13-20% slower on malisx2, the
ND Demo 41.4 fps against 45.7.

The choice below replaces the old "Vulkan Driver Toggle" tool, which is no
longer in the image.

**One driver per process.** Both drivers cannot be listed to the Vulkan loader:
libmali takes DRM master on the panel's card once it has enumerated its GPU,
and malisx2's VK_KHR_display swapchain then fails ("Failed to create
KHR_display surface" in RetroArch), whichever is listed first. A second GPU in
an emulator's adapter list would not work, so:

- **libvulkan_darkos.so** (`vk-driver-select/`, `build_vkselect.sh`) is the only
  driver the loader sees (`rk_vk.json` points at it). It loads libmali or
  malisx2 and hands the loader that driver's three entry points. Which one: the
  first that says of `DARKOS_VULKAN` in the program's environment (`mali`,
  `malisx2`, or a driver library's path), a line `<program> = <driver>` in
  `/etc/darkos/vulkan.conf` (the program by its executable), that file's
  `default`, mali. A driver that does not load falls back to mali, with a line
  on stderr; `DARKOS_VULKAN_DEBUG=1` reports every choice.
- **ES, EMULATOR SETTINGS > a system > VULKAN DRIVER**: AUTO, MALI, MALISX2
  (`es-0002-vulkan-driver-choice.patch`), kept as `<system>.vulkan` like the
  governor. ES launches the system's games with `DARKOS_VULKAN` set to the
  choice; AUTO leaves it unset, so vulkan.conf decides. The choice is offered
  for the systems the file's `es_systems` line names, comma-separated and
  editable on the device: now 3ds, n3ds, ps2, ppsspp, pspminis, psx,
  dreamcast, naomi, Sega Atomiswave, ports and, since 1.3.1, Gamecube: the
  systems whose emulators draw with Vulkan (1.3 left Gamecube out, as
  Dolphin did not start on malisx2). A system ES shows under an alias is
  named by the alias (n3ds is the 3DS folder of a GammaOS card,
  es-aliases.py). The update keeps a user's
  vulkan.conf and adds the line if it has none
  (`ota/hooks/1.3-vulkan-es-systems.sh`), and from 1.3.1 Gamecube to a line
  that has not got it (`ota/hooks/1.3.1-vulkan-gamecube.sh`). Like the other rows of that menu, a
  change is saved when the menu is left with B; START closes all menus without
  saving.
- **PortMaster:** a port that runs its game with `$ESUDO` (sudo with a
  `--preserve-env` list) would lose `DARKOS_VULKAN`; `portmaster-e2big.sh` adds
  it to that list in `mod_dArkOS.txt` when it is set.

Enumerating malisx2 costs nothing measurable (vulkaninfo 5-10 ms, 8 KB).

The image builds malisx2 itself (`build_malisx2.sh`, from 1.3's build on):
1.3 has bmdhacks' commit eae9663 with Mesa at the commit it pins and our
change of the device name to "Mali-G52 (malisx2)" (then
`malisx2/patches/`), -O3 for the Cortex-A53, full LTO and a profile
(`malisx2/pgo/`), installed at `/usr/lib/aarch64-linux-gnu/malisx2/`. After
1.3 the source is our fork, github.com/mamaich/malisx2-rg52mini (077e853:
eae9663 and the device name as a commit), where the driver work goes on.
1.3.1 takes its 5eccf34: occlusion query pools accepted, every result 0
samples (0488ddb); storage-buffer reads in v7 vertex shaders bounds-checked
(80994d9), which costs ARMSX2 nothing measurable (TODO.md); fewer compute
copy jobs per draw on v7 (4c73962-fa1831a: a stage's tables and FAU block
kept across pipelines that would build them the same), with the profile
retrained on it and on ARMSX2's drawing as well (2026-10-26); then
descriptor tables and uniform words filled on the CPU, forward pixel kill
kept under rasterization order access and AFBC for textures (9ca29fd-b74141a),
with the profile retrained again (2026-10-27); and the G52 tiler's lowest
hierarchy levels with AFBC for D32 depth attachments (d2afec5). ARMSX2 on fa1831a against
b1c114a, each with its own profile: God of War 4.5% faster (frames
100-1500), Jak and Daxter 1.3%; on b74141a against f35bacc: God of War on
the ship 1.7% more, Jak and Daxter in game 0.8%, and Castlevania in Azahar
1.4-2.3%; d2afec5 then God of War on the ship 1.7-2.7% more. 1.4 takes
262ad73: Index-Driven Vertex Shading on v7 (98a8fdf), one Indexed Vertex job
per draw instead of a Vertex and a Tiler job, as libmali does
(`MALISX2_V7_IDVS=0` for the old pair); Castlevania 1.9 and 5.6% faster, the
PS2 games within the spread. It changes malisx2's pipelineCacheUUID, so after
the update every game compiles its pipelines once more on malisx2. With LTO and a profile it compiled Castlevania's 368 pipelines in
7.4 s instead of 8.1 (2026-10-25).

**Dolphin on malisx2 (1.3.1).** Two things kept Dolphin off malisx2 in 1.3.
Its Vulkan backend makes a pool of 512 occlusion queries at start and gave
up the whole backend when malisx2, which had timestamp pools only on the job
manager, refused it ("Failed to create query pool"). And without geometry
shaders and wide lines it expands lines in the vertex shader, reading the
other end of a line as `input_buffer[vertex_id - 1]`, while malisx2's v7
Vertex job shades every index from the draw's smallest to its largest, those
no index uses as well: for one of them the read went 16 bytes before Dolphin's
vertex buffer and faulted the GPU (ND Demo, its first draw into the EFB).
malisx2 0488ddb accepts occlusion pools (every result 0, as with perf queries
off) and 80994d9 bounds-checks storage-buffer reads in v7 vertex shaders;
`dolphinsa-patch-014` lets Dolphin run without the pool when a driver refuses
it, with perf queries off (their results read 0; with libmali nothing
changes). On the console: ND Demo 41 fps, Metroid Prime runs and draws right;
other GameCube games are not tried yet (TODO.md, "Dolphin on malisx2").

## gl4es ports rendered black into their own textures: gl4es-fix (after 1.0)

**The problem (1.0 and earlier).** In Don't Starve the HUD showed, but the
world was black. It is a PortMaster port on box64 and gl4es, which turns the
game's OpenGL 2.1 into GLES2 for libmali. Vulkan and the rotate layer play no
part in it.

**The cause.** The game renders into textures of its own (FBOs), the light
map among them, and asks for `GL_LINEAR` on them. A trace of the GLES calls
gl4es makes showed the following.
- gl4es creates each such texture with `glTexImage2D` of level 0 without data.
- gl4es never passes the minifying filter down to the driver.
- All 13 render-target textures of the game stayed at the GLES default,
  `GL_NEAREST_MIPMAP_LINEAR`.
- They have no mipmaps, so they are incomplete, and sampling an incomplete
  texture returns black.

Don't Starve multiplies the world by the light map, so the world was black.
The postprocess pass also reads the scene through such a texture.

The trace: every FBO was complete (`GL_FRAMEBUFFER_COMPLETE`). The light map
was cleared to daylight (1.0, 0.9, 0.62). Forcing full light in the world
shaders brought the world back, and so did giving the textures `GL_LINEAR`
underneath gl4es, with lighting and shadows intact.

gl4es keeps its own copy of each texture's sampler state and calls
`glTexParameteri` only when its copy differs from what it last sent; for
these textures the copy and the driver disagree. The upstream report is
drafted in `gl4es-fix/UPSTREAM.md`.

**The fix.** `gl4es-fix/` builds a `libGLESv2.so.2`
(`/usr/lib/aarch64-linux-gnu/gl4es-fix/`):
- it forwards every `gl*` of the image's libMali; the forwarders are generated
  from its symbol table at build time;
- after a `glTexImage2D` of level 0 without data, it sets `GL_LINEAR` if the
  texture is left with a mipmap filter.

That turns a texture that could only read black into a working one. A program
that allocates level 0 empty, builds mipmaps itself and never sets the filter
again would lose mipmap filtering on it: a softer picture, not a broken one.

gl4es loads its GLES library from `LIBGL_GLES`, and nothing else reads that
variable. `portmaster-e2big.sh` adds to `mod_dArkOS.txt` an export of it,
unless the port set its own. So only ports on gl4es get the fix. EmulationStation,
RetroArch, SDL and native ports keep linking libmali directly.

**Don't Starve, port side.** The postprocess shaders also failed to compile
under gl4es ("S0020: Array subscript too big"): `SAMPLER[SAMPLERCOUNT]`, with
`SAMPLERCOUNT` a sum of nested macros, came out as 2. Writing the number into
each `postprocess*.ksh` fixes them. That is a change to the game's data on the
card, not to the image. The cause, found on 2026-10-25, is gl4es's own shader
preprocessor: it substitutes `#define`s from both branches of an `#if
defined(...)` it does not evaluate (TODO.md, gl4es; the patch is in
`gl4es-fix/`). Ports bring their own gl4es, so the image now has a fixed one:
- `build_gl4es.sh` builds gl4es (pinned commit, the patch, built as
  PortMaster's are: no X11, GLX stubs, its EGL wrapper, GBM) into
  `/usr/lib/gl4es`;
- gl4es ports source `libgl_${CFW_NAME}.txt`, else `libgl_default.txt`, and
  PortMaster has no dArkOS one, so `portmaster-e2big.sh` writes
  `libgl_dArkOS.txt`: the default file's lines, then `/usr/lib/gl4es` for a
  port that has no `gl4es` or `gl4es.aarch64` folder of its own;
- so a port's own (broken) gl4es still wins, and renaming its gl4es folder
  (`gl4es.off`, so it can be put back) or deleting it is all a user has to do
  to take the fixed one; the release notes say so. Without it, the loader would
  find Debian's `libGL.so.1`, which needs X. Our Don't Starve script already
  looks in `/usr/lib/gl4es` after its own folder.

Checked on the console with Don't Starve, bloom and distortion switched on:
with the port's gl4es the screen stays black: 6 of the 8 postprocess shaders,
which the game compiles at start whatever the settings, fail with S0020, and the
one it then draws the frame with is among them. With the folder renamed the
port takes `/usr/lib/gl4es`, all 8 compile and a loaded save looks right. With
bloom and distortion off, as the port runs by default, the frame goes through
the plain shader, one of the two that compile on the port's gl4es.

## The "overclock" governor (after v10062026)

EmulationStation's governor list for emulators (per system, per game, and the
default) is now: performance, **overclock**, ondemand, powersave. The mode is
taken over from GammaOS Next, where it proved stable.

| | performance | overclock |
|---|---|---|
| CPU label (real on bin L3) | 2016 (~2007 MHz) | 2208 (~2135-2165 MHz) |
| GPU label (real) | 900 | 1000 (~960 MHz) |
| memory | performance | performance |
| soc-thermal trips | 75/85 C | 83/93 C |

**Parts:**
- **The BL31 table with the 2208/1000 labels.** It is already in
  `BSP/uboot-rg52mini.img` (uboot-oc-stocktee-shmkept). BL31 only takes
  rates it has, and the kernel clamps to its SCMI list.
- **The OPPs, cherry-picked from the GammaOS kernel** (kernel_rk3562
  `cc3acb8c3`, `76814b747`, `8311d0e31`):
  - CPU 2208 MHz at 1.15 V, `turbo-mode`, so the kernel never picks it
    while cpufreq `boost` is 0;
  - GPU 1000 MHz at 0.975 V;
  - the 2016 MHz step on bin L3 at 1.125 V instead of 1.100 V. At 1.100 V
    it really ran at ~1950 MHz (~1910 when hot); this is in every mode.
- **The Mali `POWER_DOWN_FREQ` of 300 MHz.** It is already in the kernel and
  is required: the overclock table has no GPU 200 MHz (see above).
- **`/etc/udev/rules.d/99-gpu-oc-cap.rules`.** Devfreq has no turbo flag and
  allows the top step from the start, so the GPU is capped at 900 MHz as
  soon as it appears, before the boot animation.
- **`scripts/perfoc on|off|status`.**
  - `on` sets boost, the CPU, GPU and memory governors and ceilings, and
    raises the trips;
  - `off` undoes the boost, the GPU ceiling and the trips, and leaves the
    governors to perfmax and perfnorm;
  - `perfmax overclock` calls `on`, and falls back to performance where the
    OPPs or the table are missing;
  - every other governor, `perfnorm` (game exit and boot) call `off`, so
    overclock never outlives a game or a reboot.
- **ES:** `build_emulationstation-rk3562.sh` adds "overclock" to the fixed
  list in `es-app/src/SystemData.h`. The cache key gets `_oc`.

**Thermal trips.** In overclock they are 83/93 C, not off:
- above 95 C, `rockchip,high-temp` drops the CPU to 1800 MHz until the chip
  cools below 90 C;
- the GPU has no such limit, so power_allocator stays on;
- the 10 C window stays as it is, because the kernel computes the
  regulator's coefficients from the boot-time trips.

**Tested on the device**, with the new DTB swapped into /boot:
- **After boot:** `boost` is 0, 2208000 is only a boost frequency,
  `cpuinfo_max` is 2016000, the GPU `max_freq` is 900000000 with 1000000000
  listed, and there is no "failed to set power down rate".
- **CPU speed:** the same Python loop pinned to one core took 4.26 s in
  performance and 3.97 s in overclock (+7.4 %, matching ~2007 -> ~2150 MHz
  real). So BL31 does take the label.
- **In a game:** DSperate (Pokemon demo) was started through
  `perfmax overclock` and ran for 3 min at CPU 2208 and GPU 1000 with boost
  1. It reached 60 C, far from the trips.
- **After the game:** perfnorm brought back boost 0, the GPU on
  simple_ondemand with a 900 MHz ceiling, and the GPU idle at 300 MHz.

**Not yet checked:**
- the ES menu entry on screen; the ES of 1.2 does have it in its list
  (performance, overclock, ondemand, powersave);
- a long session in a heavy game;
- other boards: GammaOS measured bin L3, and a chip that does not take
  2208/1000 shows it only in this mode.

## A desktop-fullscreen window got a scaled surface — in 1.0 to 1.2, fixed in 1.3

Seen on 2026-10-19 with a GTA San Andreas port (the gtasa Android wrapper, R36S
lineage): only the lower left of the picture, about a quarter, magnified to the
whole screen. Its `config_gtasa.txt` has `screen_width -1`, `screen_height -1`
("screen mode: AUTO"), and then it creates its window with 0x0 and
`SDL_WINDOW_FULLSCREEN_DESKTOP` and takes `SDL_GL_GetDrawableSize`, which was
right: 1280x720. The surface under it was not. `sdl2-patch-0008` (scaled modes)
picks the mode closest to the window's size when the window is made, which for
0x0 is the smallest scaled mode, 640x360, and its check for fullscreen,
`(flags & SDL_WINDOW_FULLSCREEN) == SDL_WINDOW_FULLSCREEN`, holds for
FULLSCREEN_DESKTOP too, which has that bit. So the GBM surface was 640x360, the
game drew a 1280x720 viewport into it, and the RGA pass scaled the 640x360 it
got onto the panel. Scaled surfaces are now for exclusive fullscreen only
(`(flags & SDL_WINDOW_FULLSCREEN_DESKTOP) == SDL_WINDOW_FULLSCREEN`).

The size changed under the app: SDL said 1280x720 until the first swaps, where
KMSDRM remakes the surfaces it marked dirty, and the scaled size after them. So
any desktop-fullscreen window asked for smaller than the panel was hit, if the
app took the size once. On the device, with a GL window, its drawable before
and after the context and after three swaps, against EGL_WIDTH/HEIGHT:

| window asked for            | 1.2 SDL, after the swaps | fixed SDL          |
|-----------------------------|--------------------------|--------------------|
| 0x0, FULLSCREEN_DESKTOP     | 640x360 (1280x720 before)| 1280x720 all along |
| 640x480, FULLSCREEN_DESKTOP | 640x480 (1280x720 before)| 1280x720 all along |
| 854x480, FULLSCREEN         | 854x480                  | 854x480            |

The gtasa port, back on AUTO, was whole with the fixed library. A 0x0 window in
exclusive fullscreen still gets 640x360, the smallest mode SDL finds for it, but
SDL says so from the start.

On 1.0 to 1.2, give such a port its size: for gtasa, `screen_width 1280` and
`screen_height 720` in `config_gtasa.txt`, which made it whole on the device.

## SDL2 under KMSDRM re-raised every SIGSEGV (fixed in our SDL after v10062026)

Found with the native Halo: Combat Evolved port (kirklandsig's
halo-ce-anbernic-rg35xx, built for Knulli and aurknix), which crashed on
dArkOS at "starting main menu music" and runs with the workaround below.

**What happens.** When SDL2 reads the keyboard through evdev on a console — the
KMSDRM case, a program run as root or with access to the VT — it mutes the
console keyboard (`KDSKBMODE K_OFF`) and, so that a crash cannot leave the
console dead, installs `kbd_cleanup_signal_action` for the fatal signals,
SIGSEGV and SIGBUS among them (SDL 2.32, `src/core/linux/SDL_evdev_kbd.c`).
That handler does not look at the fault: it puts the previous handler back,
restores the keyboard and calls `raise()`. A program that expects SIGSEGV as
part of normal work therefore never sees the real fault. Its own handler gets
the re-raised signal instead — `si_code` SI_TKILL, the PID where the fault
address should be — cannot recognise it, and treats it as a crash.

Halo protects some of its guest pages read-only to notice writes (texture
write-watch, `port/android/host/host_memory.c` upstream); the first write
faults, and the game's handler is meant to unprotect the page and carry on.
Under gdb on the device: `host_memory_watch_protect` makes 0x8605d000
read-only, the guest writes to it, and `tgkill(SIGSEGV)` then comes from
`libSDL2` inside the signal handler. Under Wayland SDL does not touch the
console keyboard and installs nothing, which is why the port worked there and
why its author found that it "only works with SDL2 on Wayland". The bundled
libmali and the SDL build make no difference; both were swapped and tested.

**Workaround:** `SDL_NO_SIGNAL_HANDLERS=1` in the program's environment. SDL
then skips `kbd_register_emerg_cleanup`; the cost is that a crash can leave the
console keyboard muted until the next reboot (EmulationStation does not use
it). For Halo that is one line in `Halo.sh`, which its author can carry for
every KMSDRM system. `SDL_INPUT_LINUX_KEEP_KBD=1` avoids it as well, by not
muting the keyboard at all.

The same applies to anything else that relies on its own SIGSEGV handler under
SDL on KMSDRM: emulators with fastmem or JIT fault tricks, box64, Mono.

**Fix, in our SDL from the image after v10062026:**
`rk3562_core_builds/patches/sdl2-patch-0007-evdev-kbd-chain-signals.patch`.
The handler no longer re-raises. When the program has a handler of its own, SDL
calls it directly with the original siginfo and ucontext, so a handler that
fixes the fault and returns resumes the program, as it would without SDL. For
the time it runs, the console keyboard is given back, because the handler may
end the process. It is muted again only if the program goes on: SDL's handler
is still installed and the signal is not pending. A handler that decides on a
crash usually puts SIG_DFL back and raises; the raised signal stays pending
until the handler returns, and then kills the process with the keyboard
restored. Without a handler of the program's, a hardware fault returns and
repeats under the default action, so the core dump has the real address.
Signals sent with kill() are re-raised, as before.

Tested on the device with a small program and with Halo:
- the program installs its SIGSEGV handler before SDL_Init and runs three
  cases: a write-watch page, a crash with its own handler, and a crash with
  no handler;
- old SDL: the write-watch case dies, and the handler sees `si_code` -6;
- patched SDL: 1000 write-watch faults are handled with the right address;
- with both, the two crash cases end the process with signal 11, and the
  console is K_UNICODE afterwards;
- Halo, run without the variable, reaches the campaign, and SIGTERM now quits
  it cleanly (exit 0). `SDL_NO_SIGNAL_HANDLERS` had also turned off SDL's
  SIGTERM-to-quit handling, so it used to be killed (143);
- EmulationStation and DSperate (Vulkan) run on the patched library.

The console stays in K_OFF after a game exits. This is the same with the old
library and with the new one, and the same after EmulationStation is stopped,
so it does not come from this patch.

Programs that bundle their own libSDL2 still need the variable, and so do
images up to v10062026. Upstream SDL2 (release-2.32.x) and SDL3 (main) still
have the re-raising handler.

## Build flags targeted the wrong CPU

`rk3562_core_builds` came from `rk3566_core_builds`, and the RK3566 is
Cortex-A55. This is an A53 — a different pipeline and an older architecture
level. Sixteen scripts said `-mtune=cortex-a55`, which only costs scheduling,
but `uae4arm.sh` passed `-mcpu=cortex-a55+crypto+crc` with no `-march`, which
raises the baseline to ARMv8.2-A and lets the compiler emit FP16, dotprod and
LSE atomics that this CPU does not have.

Fixed in `mamaich/rk3566_core_builds` `7290d75`. `+crc` stayed (the device
reports `CPU features: detected: CRC32 instructions`); `+crypto` went, because
AES/SHA are optional on Cortex-A53 and nothing in the device's own logs says
this part implements them.

The `uae4arm` half of that is a repository fix with no effect on this image:
dArkOS calls `builds-alt.sh` with twenty-five targets and `uae4arm` is not one
of them. Amiga on the device comes from somewhere else entirely — `amiga.sh`
runs `/usr/local/bin/amiberry.sh`, and RetroArch downloads a prebuilt
`puae_libretro.so` from `christianhaitian/retroarch-cores`. Neither is compiled
here, so neither ever saw the wrong `-mcpu`.

The `-mtune` half does reach real binaries — RetroArch, mupen64plus and the
rest — but only those that actually rebuild. See the next section.

### Prebuilt cores and one recipe still carry A55 code (2026-10-16)

Found by loading every RetroArch core on the device with null drivers and no
content, then disassembling everything in the 1.2 test image for ARMv8.1+
instructions. The Cortex-A53 has none of them. LSE atomics guarded by GCC's
outline-atomics test (`ldrb w16` / `cbz w16` just before) are fine — RetroArch,
flycast standalone, PCSX2, Dolphin have those — and Go's filebrowser tests the
CPU its own way and runs. Inline ones are SIGILL whenever they execute:

| | inline LSE | on the device |
|---|---|---|
| `same_cdi` (CD-i) | 816 | SIGILL on load |
| `mupen64plus_next` (N64) | 676 | SIGILL on load (`ldaddal` at 0x99910) |
| `flycast`, `flycast_rumble` (64-bit) | 300 | loads; a game stops within seconds, its own handler printing "SIGSEGV ... not in vram" (`ldaddal` at 0x14441e4) |
| `uae4arm` (Amiga) | 134 | not run |
| `puzzlescript` | 35 | not run |
| `mednafen_psx_hw` (Beetle PSX HW) | 21 | not run |
| `/opt/gametank/GameTankEmulator` | 82 | built here: fixed, see below |

The cores come prebuilt from christianhaitian/retroarch-cores, branch rg503,
built for the RK3566's Cortex-A55; every older build of `mupen64plus_next` and
`same_cdi` on that branch fails the same way, so this is not a regression. None
of them is a default any more: N64 starts `parallel_n64`, PSX `pcsx_rearmed`
(32-bit), and Dreamcast, Naomi and Atomiswave now the standalone flycast
(Naomi and Atomiswave used to start the 64-bit libretro one, which is why
`kov7sprt` stopped; Dreamcast the 32-bit `flycast32_rumble`, slower — see
"Dreamcast, Naomi and Atomiswave" in PERFORMANCE.md).

Four of them are now built here for the A53, by `build_a53_core` in
`build_retroarch.sh`, over the downloaded ones; each is cached by its recipe
and upstream head. All were built in a trixie arm64 chroot on 2026-10-16 and
run on the device with no unguarded LSE instruction left:

| core | change in `rk3562_core_builds` | on the device |
|---|---|---|
| `mupen64plus_next` | `mupen64plus-nx.sh` no longer turns the Makefile's a53 into a55 (c58d3ad) | AeroGauge, about 49 fps, GLES on the Mali |
| `flycast`, `flycast_rumble` | `flycast.sh` checks out 4c293f3, the commit retroarch-cores builds (master no longer takes the per_pixel patch); the RK3566 block of the Makefile patch says cortex-a53; GCC 14's new C errors are warnings again through `CC`, since the Makefile resets `CFLAGS` (eb88dab) | Crazy Taxi 40-43 fps; Knights of Valour - The Seven Spirits past the point where the old core died. In the image (build 20261016184025) both run in RetroArch and in retrorun |
| `puzzlescript` | none | starts |
| `same_cdi` | none (default flags, ARMv8.0) | loads; no CD-i disc on the card to go further |

The first build with them takes about two hours longer, mupen64plus_next most
of it; later ones take the cache.

Still rg503's, and still A55 code: `uae4arm` (its `capsimg.so` step fails with
the current upstream) and `mednafen_psx_hw` (beetle-psx no longer links:
`retro_vfs_file_*_cdrom` undefined). Neither is a default.

`stella` (the second Atari 2600 core; `stella2014` is the default and works)
cannot load at all: every rg503 build of it needs `libiconv.so.2`, GNU libiconv,
which Debian does not have.

GameTank was the one built here: `gametank-patch-002-performance-tweak.patch`,
rewritten upstream in July, put `-mcpu=cortex-a55` back without `-march`. It
says `cortex-a53` now (`rk3562_core_builds` 0bb7d88), and `build_gametank.sh`
keys its cache on the recipe as well, so it was compiled again; the 1.2 image
has that build.
Other patches with `-mcpu=cortex-a55` either also pass `-march=armv8-a...`,
which keeps the baseline at ARMv8.0 (yabasanshiro), or belong to cores this
build downloads instead of compiling.

### retrorun never ran on the RG52 Mini (2026-10-16)

ES offers retrorun and retrorun32 for Dreamcast, Naomi, Atomiswave and Saturn.
On this device every one of them failed, three times over:

1. **retrorun32 could not start**: `libcurl.so.4` missing. It is built in
   Arkbuild32, where curl comes with the dev packages, but nothing put the
   armhf library in the image. `needed_packages32.txt` now has `libcurl4t64`.
2. **No gamepad, then SIGSEGV.** retrorun knows pads by name and did not know
   `rk3562-joystick`; told the name, it then found no event node and built a
   `std::string` from NULL. `retrorun.cfg.rk3562` now sets
   `retrorun_extra_retrogame_name` and `retrorun_extra_evdev_name`.
3. **Upside down.** The launchers gave it `DEVICE_NAME=RG351P`, whose rotation
   (270 degrees for a landscape game) suits the RG43H Pro's panel; the RG52
   Mini's is mounted the other way round and needs 90. Our retrorun has an
   `RG52MINI` profile, the RG351P's choices turned 180 degrees
   (`retrorun-patch-0002-rg52mini-rotation.patch`, rk3562_core_builds 9e532b9),
   and the launchers pass it when `~/.config/.DEVICE` says RG52MINI.
   `build_retrorun.sh` keys its cache on the recipe too, so the patch gets in.

Checked on the device: with the two config lines retrorun32 runs Crazy Taxi;
the patched retrorun shows the NES 240p test suite upright with RG52MINI and
upside down with RG351P. Its screenshots and on-screen messages go through the
same rotation change and were not looked at. retrorun prints "ALSA mixer
control 'Playback' not found", so its own volume keys do nothing here.

In the image (build 20261016184025, 2026-10-17) every retrorun and retrorun32
core ES offers ran Crazy Taxi and Knights of Valour - The Seven Spirits
upright. retrorun reads only the pad named in `retrorun_extra_evdev_name`, so
input injected through another device (a uinput pad, as the test tools use)
does not reach it.

## The build cache is keyed by upstream version, not by build flags

Each component caches to `Arkbuild_package_cache/<chipset>/<name>.tar.gz` with a
`.commit` file holding its key. For most components that is the upstream tag
or commit alone: change a compiler flag, a patch, or anything else on our side
and the key does not move, so the cache is restored and the change does not
happen. Most recipes add a hash to the key, so a change there rebuilds:
of the recipe and its patches (dolphinsa, dsperate, easyrpg, ecwolfsa,
flycastsa, gametank, gzdoom, hypseus-singe, malisx2, mednafen, openbor,
openmsx, pcsx2sa, ppssppsa, retrorun, scummvm, solarus, xroar,
yabasanshirosa), of the
recipe alone (fake08, whose two patches are upstream's and not in its key)
or of the patches alone (emulationstation-rk3562, ogage, and retroarch,
whose `retroarch.sh` is not in it; RetroArch32's also hashes SDL2's recipe
and patches, because its tarball carries the armhf libSDL2); `git grep -l sha1sum -- 'build_*.sh'`. Kodi's key is a constant in
`build_kodi.sh`, bumped by hand.

Two consequences worth knowing:

* A flag change only lands in components that rebuild for some other reason.
  After the Cortex-A53 fix, everything restored from the 2026-09-15 cache was
  still tuned for the A55. Delete the relevant `.tar.gz` and `.commit` pair to
  force a rebuild.
* `build_yabasanshirosa.sh` was worse: it derived its key by curling the
  **upstream** christianhaitian script for its `TAG=`, not our fork's, so
  editing the tag here left the key unchanged and a stale tarball was restored
  over the new build. It reads our recipe's `TAG=` now, and since 2026-10-24
  adds a hash of the recipe and its patches, so a new patch rebuilds it.
* Eight more did the same until 2026-10-28 (scummvm, gzdoom, mednafen,
  openbor, openmsx, solarus, xroar, easyrpg): christianhaitian moved ScummVM
  to v2026.4.0 on 2026-10-22, ours had not, and that day's build compiled
  2026.3.0 and cached it as v2026.4.0, which 1.3 and 1.3.1 ship. They read
  our recipe and hash it with its patches now.

A failed build used to be cached the same way — see below.

## Failures used to be cached like successes

Until 2026-09-16 a component that produced nothing still had its empty
`/opt/<name>` tarred up, 45 bytes, under a key that matched. Every later build
restored the failure instead of retrying it, and printed a line saying it was
using the cache. Six entries were in that state: kodi, bluealsa, freej2me-plus,
yabasanshiro, ecwolf, gametank. The build started to test Kodi would have
skipped Kodi.

`build_kodi.sh` now caches only when `/opt/kodi` holds an ELF (`build_ecwolfsa.sh`
likewise since 2026-10-15), and the audit
lists every cache tarball under 20 KB, which covers the other thirty-odd
components without editing each script. The bad entries were moved to
`Arkbuild_package_cache/failed-2026-09-15/`.

If a component is mysteriously missing and the log says it came from the cache,
look at the size of its tarball first.

Since 2026-10-22 a failed build in the chroot stops the image build instead
(`verify_action` after each one in the recipes, and RetroArch's retries end
the build rather than going on), so no archive of it is made. The same day
showed a second way in: while the network was down the clone of mupen64plus
failed, and so did the `curl` that gives its key, so the entry was saved with
an empty key and a 169-byte archive. The next build's check got no answer
either, found "" equal to "", and the image came out without mupen64plus.
`utils.sh` now drops, as a build starts, every entry whose key is empty or
"null" or starts with one of them, and that package is built again.

A build that stops at every network blip would rarely finish, so git in the
chroots (`call_chroot`, `call_chroot32`, `chroot_run`) tries a clone, fetch,
pull, ls-remote or submodule update again after 15, 30 and 60 s, as git on the
host does; what a failed clone left behind goes first. It is a wrapper in
`/root/.darkos-git`, first in the chroot's PATH, which `write_rootfs*.sh`
remove before the root filesystem is written.

Checked on 2026-10-23: a full build with both changes (88a32cb, 8 h 15 min)
stopped nowhere, gave up only on the four packages every build gives up on
(libfuse2, libpcap0.8, liblc3-0, libreadline7), and its image has
mupen64plus and no `/root/.darkos-git`.

## The Bluetooth driver freed every failed packet twice — fixed

`bt_sdio_recv()` in `drivers/net/wireless/aic8800/aic8800_fdrv/btsdio.c` called
`kfree_skb()` whenever `hci_recv_frame()` returned an error. `hci_recv_frame()`
owns the skb and frees it on each of its own error paths — `-ENXIO` when the
hci device is neither `HCI_UP` nor `HCI_INIT`, `-EINVAL` on an unknown packet
type — so every one of those was a double free.

It fires on shutdown or reboot with Bluetooth on: the hci device goes down
while frames are still arriving, and the log fills with `hci_recv_frame fail
-6`, 150 to 200 per shutdown. The freelist is corrupted and the kernel dies a
moment later somewhere else entirely — in `__skb_try_recv_from_queue()` from
`netlink_recvmsg()`, or inside `kmem_cache_alloc()` reached from `skb_clone()`
while `device_del()` broadcast a uevent. Nothing points back at Bluetooth,
which is why it survived this long.

Two details worth keeping:

* `btsdio.o` is in `aic8800_fdrv.ko` unconditionally. `CONFIG_SDIO_BT=y` is
  hardcoded in the driver's own `Makefile`, not taken from the kernel config,
  so grepping `.config` for it finds nothing and proves nothing.
* `alloc_skb()` failure was logged and then ignored, so the `memcpy(skb_put(…))`
  below it dereferenced NULL. `GFP_ATOMIC` on 2 GB under a heavy game with a
  gamepad connected can fail. Fixed in the same commit.

Fixed in `kernel_rk3562` as *aic8800: stop freeing an skb that
hci_recv_frame() already freed*. The bug was first tracked down in the Android
kernel for this board (`mamaich/kernel_rk3562_rg52mini` 0c34dd591), where the
panic handler overwrote the requested reboot mode and the device stopped
rebooting into eMMC. The same code is in every copy of this vendor driver on
GitHub — `radxa-pkg/aic8800`, `D-Robotics/x5-kernel`,
`unifreq/linux-6.1.y-rockchip` — so it is worth carrying to any other tree
that uses this chip.

## Revision A: black screen after the splash — fixed

Reported from a revision A unit (RK915 Wi-Fi) running v09292026: the
bootloader splash appears, then the screen stays black until the power button
is pressed twice. The system is running all along; two presses are one
suspend/resume cycle through ogage, and after it the picture is there.

What has been ruled out, by test rather than argument:

* **The image.** The published v09292026 and the one confirmed working on a
  revision B unit are byte-identical (sha256 `2e50bda0…2f2150`).
* **The bootloader.** v09292026 with the v09162026 `uboot` partition — old
  U-Boot and old OP-TEE, everything else new — still goes black. Between the
  two releases only U-Boot and OP-TEE differ inside the FIT; ATF, the
  bootloader's DTB and the idbloader are identical.
* **`logo_kernel.bmp`.** Not on the boot partition.

Between the releases the boot partition differs in `Image`, `uInitrd` and the
DTB, and the DTB only by the added `firmware/optee` node. So the kernel is the
remaining suspect — unless v09162026 also goes black on that unit, which has
not been asked yet and would make this no regression at all.

**What the black screen is**, found on a revision B unit, where it can be
produced on purpose by blanking and unblanking fb0 before EmulationStation:

| | gap between off and on | DSI, PHY, VOP runtime-suspended | screen |
|---|---|---|---|
| blank, fbcon restores the mode | 38 ms | no | black, backlight on |
| same, panel supply held off 500 ms (`off-on-delay-us`) | 43 ms for the DSI host | no | black |
| blank 3 s with fbcon unbound | 3 s | yes, within 0.5 s | **works** |
| suspend/resume | 1.8 s | yes | works |

**The runtime-PM explanation first given here was wrong.** The DSI host and
the D-PHY are in no power domain (`rk3562.dtsi`; only the VOP is, in PD_VO),
and the "DSI host suspended" that panel-kick waited for was reached on the
very first check. What cured the panel was the time it stayed off — about
0.24 s in panel-kick. Measured in the GammaOS Next port on revision B, with the
VOP still cycled in 41 ms: 120 ms off → black, 150 ms → picture.

**And why a short off breaks it is in the device tree.** Every stock tree —
`flash-v10.dtb`, `flash-v14.dtb`, `live.dtb` from eMMC, the vendor
`rk3562-ro520c-lp3x-v10/v14-linux.dtb` — powers the panel down with

    05 80 01 28   display off, then 128 ms
    05 1e 01 10   sleep in, then 30 ms

This tree, inherited from the SyachOS-derived one, had both delays at zero, so
panel-simple asserted reset and cut the supply straight after the commands.
With the stock delays a 41 ms off/on gives a picture 3 times out of 3; without
them, black 4 out of 4 (also measured in the Android port).

The panel's own supply (`vcc3v3_lcd_n`) is not the issue, and the RK628 bridge
is not either: its fb notifier does react to blank events, but with the bridge
made silent (moved to an address nobody answers) the result was the same.

**Fix** (`kernel_rk3562`), first published as the pre-release v10032026-test,
confirmed working on a revision A unit by its owner, and in the v09292026
assets since 2026-10-04:

* `23506d78f` — the stock `panel-exit-sequence` delays in `rk3562-darkos.dtsi`;
* `7483a4f83` — from the Android port: a minimum DSI off time,
  `dw_mipi_dsi.rg52_min_off_ms`, 500 ms by default. A re-enable sooner than
  that after a power-off waits out the rest and logs `link off N ms, waiting
  M ms more` — so a revision A log will say whether a fast cycle happened.
  Boottime, so suspend counts as off time. It also moves the DSI host reset
  after `pm_runtime_get_sync()`, where the APB clock is actually running.

What makes revision A go through a fast cycle at boot is still not proven.
Candidates: an extra off/on (closed by the fix above), a failed warm takeover
of the U-Boot link on the first enable (which the off-time wait cannot touch,
since nothing was switched off before), or rk915 power-cycling the Wi-Fi rail
around the time the display first comes up.

Worked around by `panel-kick` (`scripts/rk3562/panel-kick.sh`), in the
v09292026 assets from 2026-10-03: on revision A — no HUSB311 bound, the Type-C
controller only revision B has — and before EmulationStation takes the display,
it unbinds fbcon from fb0, blanks it, and unblanks it about 0.24 s later
(the wait for `suspended` in that version returned at once — the time was what
counted), then rebinds fbcon. Only the display is touched. In the test release
it also waits for `wifi-driver-load` to finish first, on revision A only, keeps
the display off 0.6 s, and `/boot/panel-kick-off` disables it so the kernel and
DT fix can be tried on its own. Revision B exits at once. On a revision B unit, with
the panel deliberately left in the broken state, the same steps brought it
back. `journalctl -t panel-kick` says what it did; an empty
`/boot/panel-kick-force` runs it on any revision, for testing.

**Confirmed on revision A**: the v09292026 assets with this `panel-kick` were
tested on a revision A unit by its owner, and the workaround works there.

An earlier version of the same day did one full suspend/resume instead, which
worked but put the whole device to sleep for it. A fbdev blank without unbinding
fbcon does not work at all: fbcon switches the output back 38 ms later and
produces the very black screen it was meant to cure.

## Every card had the same SSH host keys — fixed in 1.2

The keys were made once, when the build installed openssh-server, and nothing
replaced them: every card written from one image, and every dual-boot card made
from it, had the same host keys, and the published image carried the private
halves. With Remote Services on, anyone could impersonate any of them.
(Found in llj503/dArkOS_ngp45ht, which fixes it in its own firstboot.)

`darkos-hostkeys.service` deletes them and runs `ssh-keygen -A` once, before
ssh can start, and leaves `/var/lib/darkos/hostkeys-done`; the image ships no
keys at all (removed at the end of `cleanup_filesystem.sh`, after the last apt
run). `make-ota.py` already leaves the keys out of packages, so a card that
gets the unit by update makes new keys once too — expect ssh's "remote host
identification has changed" from clients that knew the old ones. On the device
on 2026-10-15: 1.7 s, the ED25519 fingerprint changed, a second start is
skipped by the condition.

`machine-id` is shared the same way and is left alone: an empty
`/etc/machine-id` makes systemd treat the boot as the first one and apply the
unit presets, which would enable services the build disables.

## apt and ldconfig could take the GPU libraries back from Mali — fixed in 1.2

The build points `libEGL`, `libGLESv2`, `libgbm` and the rest at `libMali.so`,
and `libvulkan.so.1` at the BSP loader, over files Debian packages own. Two
things undid that on a device, both seen on 2026-10-15:

- **apt.** `apt-get install --reinstall libgbm1` put Mesa's libgbm back over
  the armhf link. Since 1.1 sources.list has trixie-updates and -security, and
  Mesa comes with Debian point releases, so `apt upgrade` would do it on its
  own. Mesa's gbm under the Mali EGL is a black screen for EmulationStation
  (inferred, not tried).
- **ldconfig without -X**, which every library install runs as a dpkg
  trigger. It points each soname link at the newest file with that soname:
  `libvulkan.so.1` went to Debian's 1.4.309 loader and `libwayland-egl.so.1`
  to Debian's. The 1.1 OTA client ran it the same way, so the 1.1 → 1.2 test
  update did it too. Vulkan still worked through Debian's loader (flycast,
  2026-10-15), but it is not what was tested.

`scripts/rk3562/divert-mesa.sh`, run by `cleanup_filesystem.sh` before the
links are made, diverts those files to `/usr/lib/mali-diverted/<triplet>`,
outside `ld.so.conf`, the way Debian's NVIDIA packages divert Mesa: package
files land there, and ldconfig never sees Debian's loader or wayland-egl.
spruceUI/dArkMoss diverts to `<name>.distrib` in the same directory instead,
which still leaves an ELF with the right soname where ldconfig looks. On the
device, after the diversions, reinstalling libgbm1, libegl1, libgles2,
libvulkan1 and libwayland-egl1 for both architectures and a plain `ldconfig`
left every link as the build made it, and EmulationStation and flycast on
Vulkan still ran.

The OTA client and `hooks/all-ldconfig.sh` now run `ldconfig -X`, and
`hooks/all-gpu-links.sh` puts the links back after the 1.1 client's plain
ldconfig. Diversions reach existing cards with the package database.

## Games ran on the ondemand governors unless one was chosen — fixed in 1.3

ES starts nearly every system as `sudo perfmax %GOVERNOR% %ROM%`, and
`%GOVERNOR%` is empty unless a governor was picked for the system or the game.
Then the ROM path is perfmax's first argument: it wrote that path into the
CPU, GPU and memory governors, each write failed with "Invalid argument" (three
lines in every game's journal), and the game ran on `ondemand`,
`simple_ondemand` and `dmc_ondemand`. The loading image was looked up next to
nothing as well, since the ROM was not where perfmax expected it.

`ondemand` reads an emulator that keeps one core about half busy as light
load. Super Mario 3D Land in Azahar, from one savestate:

| Governors | fps (60 is full speed; these are from before azahar-patch-006) |
|---|---|
| left as they were (the bug) | 24.0 |
| `performance` | 26.7 |
| `overclock` | 29.9 |

perfmax now puts `performance` first when its first argument is not a
governor, so a game gets what the menus call the default. DSperate (above) was
the same thing seen from one emulator.

## Emulators never ran at nice -19 — fixed in 1.2

`es_systems.cfg` starts emulators with `nice -n -19` and `limits.conf` gives
ark nice -20, but limits.conf is pam_limits, and EmulationStation is a service
without a PAM session: its limit was 0 (`Max nice priority 0` in its
`/proc/<pid>/limits`), every `nice -19` failed, and emulators ran at nice 0 —
on upstream dArkOS too. `LimitNICE=-20` in `emulationstation.service`
(from cino893/dArkOS). With it PCSX2 ran at -19 while a RetroArch kept another
core busy, and the exit hotkey still closed it within 100 ms: killer_daemon and
the like wake on input and are not starved.

## Zipped ROMs did not start in RetroArch — in 1.1, fixed in 1.2

In 1.1 a zipped Game Boy, GBA, NES, Game Gear, Neo Geo Pocket, SG-1000 ...
game went straight back to ES. RetroArch's log:

    [INFO] [Content] Loading content file: "/roms/gb/game.zip#game.gbc".
    [INFO] [Content] CRC32: 0x0.
    [ERROR] [Content] Could not read content file: "/roms/gb/game.zip#game.gbc".

Our own doing: `retroarch-patch-0022-upstream-security-fixes.patch` (adba262,
before 1.1) took the ZIP bounds checks of upstream 1235db3bd5 and 8b1e35a322
but not their follow-up f52b1196b8. The check bounded a member's
*decompressed* size by the archive's size, so every deflated member was
refused. Cores that open the zip themselves (fbneo, mame2010, flycast) and 7z
archives were not affected. The patch now bounds only the compressed size,
and the size of a STORED member, as upstream does (a6c501e).

Checked on the device on 2026-10-17 with a RetroArch built from the patched
tree: eight systems that had come back to ES each started their zipped game.
The 64-bit and 32-bit RetroArch both carry the patch; the 32-bit one was not
rebuilt for the test. The full set of patches applies to v1.22.2 as the build
applies them.

Building that test RetroArch outside the image build: GCC 14 rejects
`gl_clear()` in `gfx/drivers_context/drm_go2_ctx.c`, which upstream calls
without declaring; the image build gets only a warning there, so it needed
`#include "../common/gl_common.h"` to build by hand.

## Games in GammaOS's folders were invisible here — aliases in 1.2

On a dual-boot card /roms is GammaOS's ROMs folder, and GammaOS lays it out
for ES-DE (every folder has ES-DE's `systeminfo.txt`). Where ES-DE and dArkOS
name a machine differently — `tg16`/`turbografx`, `megacd`/`segacd`,
`fbneo`/`arcade`, `pico8`/`pico-8/carts`, `colecovision`/`coleco` ... — games
put in GammaOS's folder did not show up here: our EmulationStation reads one
`<path>` per system, and it skips a symlinked folder (it takes `d_type`, which
is DT_LNK, not DT_DIR).

`scripts/rk3562/es-aliases.py`, run by `build_emulationstation-rk3562.sh` on the
installed `es_systems.cfg`, adds a system for each of 29 such folders: a copy
of the system dArkOS uses for that machine (command, emulators, extensions,
platform, theme) with the folder's path, both in a group named after the main
system (`<group>arcade</group>`). Folders with no dArkOS emulator behind them
(switch, wii, model2, stv ...) get none. The table is in the script.

EmulationStation-fcamod already has groups, but would show every grouped
system as a group of one, a level deeper, at the end of the list, named by the
bare group name. `Emulationstation/patches-rk3562/es-0001-groups-for-alias-folders.patch`:
a group is made only when two or more of its systems have games, otherwise
each stays an ordinary system; the group takes the main system's full name,
theme and place; its folders are named after their systems. The build applies
the patches after the clone and keys the ES cache on them.

Checked on the device on 2026-10-17 with ES built from the patched tree:
with games in `/roms/arcade` and `/roms/fbneo`, Arcade opened into two
folders, Arcade and FinalBurn Neo, and Cavelon started from the second one
through fbneo; with a game only in `/roms/tg16`, TurboGrafx-16 was an ordinary
system with the game in it; systems whose alias folders are empty looked as
before. The "GROUPED SYSTEMS" option of ES can still split a group.

## Systems were sorted by short names, not by the consoles' — fixed in 1.3

With `SortSystems` = `alpha` (our `es_settings.cfg`) ES sorts the systems by
`<fullname>`, and upstream's `es_systems.cfg` has short internal names in many
of them: `N64`, `NDS`, `NES`, `C64`, `DOS`, `CPC`, `Megadrive` ... The menu
shows the theme's logos, so it read "Nintendo 64, Nintendo DS, Neo Geo, Neo
Geo Pocket, Nintendo, Nintendo 3DS". Our two `es_systems.cfg.rk3562*` now
give the consoles' names (Nintendo 64, Super Nintendo, Amstrad CPC, Commodore
64, MS-DOS, Mega Drive ...; 38 systems and the `snesna` alias). NES is
"Nintendo", as its logo reads, so it comes first of the Nintendo systems. A
group sorts by the full name of its main system (the ES patch above).

ES also shows the full name in its menus (systems to show, emulator settings,
scraper). Nothing else in the image reads it; ES keys its settings, gamelists
and collections on `<name>`, which did not change.

## PICO-8 three times in EMULATOR SETTINGS — fixed in 1.3.1

On a dual-boot card with games in both `/roms/pico-8/carts` and GammaOS's
`/roms/pico8`, EMULATOR SETTINGS listed PICO-8 three times. ES lists every
system there, and with two of the aliases above holding games there were
three: the group, which has no emulators of its own (the first entry, nothing
to choose in it), `pico-8` and the alias `pico8`, which had the same full name.
TurboGrafx-16, TurboGrafx-CD, Nintendo 3DS, ColecoVision, Pokemon Mini and
Macintosh had the same full name for their aliases too; Arcade and its
FinalBurn Neo showed one entry more.

`es-0001-groups-for-alias-folders.patch` now leaves the group out of EMULATOR
SETTINGS, and `es-aliases.py` gives an alias whose full name is its main
system's the folder after it: "PICO-8 (pico8)", "TurboGrafx-16 (tg16)". The
group's folders show the same names. Checked on the device on 2026-10-26:
PICO-8 and PICO-8 (pico8), Arcade and FinalBurn Neo, nothing else twice.

## PS2: the pause menu closed the game — fixed in 1.3

Select+X (PCSX2's `OpenPauseMenu`) showed the pause menu for half a second,
then PCSX2 closed as if the game had ended: the log shows "Pausing...", the
play time being saved and the VM released, and no crash. The fork's SDL
frontend handles a paused VM and a shut down one in the same branch of its CPU
thread loop, and with nothing pending and no FullscreenUI session, which is
every game EmulationStation starts (it passes the ISO on the command line), it
took the paused game for finished. `pcsx2/patches/pcsx2-0001` ends the frontend
only when the VM is gone; a paused one gets the input polling FullscreenUI's
idle state has, so its menu works (Resume, Close Game and the rest).
`build_pcsx2sa.sh` applies `pcsx2/patches/*.patch` and keys its cache on them.
ARMSX2, which replaced the fork in 1.3 (below), has the same frontend and
the same branch; the patch is made against its `armsx2-sdl`, and there the
pause menu stays up and Resume goes back to the game (checked on the device on
2026-10-21).

## PS2: Jak and Daxter hung and drew with glitches — ARMSX2 replaces the fork in 1.3

Up to 1.2, and in 1.3's first test builds, the image built bmdhacks' PCSX2
fork. On the RG52 Mini Jak and
Daxter (PAL) hung in the game and drew with glitches. ARMSX2
(github.com/ARMSX2/ARMSX2) is the fork the work went on in: the same arm64
recompilers and SDL frontend, a newer GS with workarounds for Mali drivers.
It draws Jak and Daxter right. `build_pcsx2sa.sh` builds it at a pinned
commit, with clang (vtlb's JIT handlers use `preserve_most`, which gcc has
not got), a profile trained here and ThinLTO of the core, 8.6% faster than
its plain build on Jak's intro (TODO: "PS2: ARMSX2 with a profile and
ThinLTO"); its binary is `/opt/pcsx2/bin/armsx2-sdl`.

Its data stays where it was: `standalone-pcsx2sa` links
`~/.config/ARMSX2` (ARMSX2's dir under `XDG_CONFIG_HOME`) to the same
`/roms/ps2/pcsx2` that `~/.config/PCSX2` was, so the PCSX2.ini of 1.2 carries
over, and with it the memory cards in `/roms/ps2/memcards`. Whether the old
fork's savestates load was not checked (the test card had none). ARMSX2
ignores the old fork's SPU2 keys in that ini (`Interpolation`, `SynchMode`,
`OutputModule`) and uses its defaults: TimeStretch sync, cubeb.

## PS2: ARMSX2 aborted every game on Mali's driver — fixed before it went in

ARMSX2 enables `VK_EXT_robustness2` (`nullDescriptor`) on every device that
has it, for its LSFG frame generation alone. With it enabled, the Mali-G52's
g29p1 driver crashes in its shader compiler
(`cmpbe_v2_compile_multiple_shaders`) building the first convert pipeline
that reads with `texelFetch` (RGBA8_TO_DEPTH32, bilinear): the GS never
starts and every game aborts at boot, in the SDL frontend and the libretro
core alike. Without the extension all 72 convert pipelines build.
`pcsx2/patches/pcsx2-0002` asks for it only while LSFG is on, as ARMSX2 does
for the fp16 extension next to it. LSFG is off by default and nothing in
the image turns it on.

## PS2: the sound broke up in heavy games — fixed in 1.3.1

ARMSX2 ran on a 50 ms audio buffer: `PCSX2.ini.rg52mini` set its output with
the old SPU2-X keys (`OutputModule`, `Latency = 100`, `SynchMode`), which
ARMSX2 does not read, so `BufferMS` stayed at its default. A frame here often
takes 80-120 ms in a heavy game and the emulation makes sound only as it
advances, so the buffer ran dry and the sound stopped until it refilled.
With `BufferMS = 100` (150 tried too) God of War sounds right to the owner
(2026-10-26). The template sets it, and the launcher adds it once to an ini
made from the old one (TODO.md, "PS2: sound in heavy games").

## PS2: "cycle rate is not at default" at start, and rates below 50% — new in 1.4

ARMSX2 warns at every start that the EE cycle rate is not at its default and
may cause slowdown. That is our setting: `PCSX2.ini.rg52mini` underclocks the
emulated EE (`EECycleRate = -3`, "50% Speed" in the menu, and
`EECycleSkip = 1`), which is a large part of why heavy games run here at all.
The warning stays (the owner, 2026-10-29).

1.4 adds 36%, 31% and 27% Speed (-4, -5, -6) below 50% in ARMSX2's Settings >
Emulation Settings > EE Cycle Rate, for all games or for one
(`pcsx2/patches/pcsx2-0005`). They are offered, not set (the owner,
2026-10-29): at -5 God of War's own time runs about a third faster at the
same frames a second, and Jak and Daxter's intro draws about a fifth more
frames, but no game has been played through at them, and a game that needs
the EE's time may break: cutscenes out of step, sound, a hang. Back to 50% if
so. Measured in TODO.md, "PS2: EE cycle rate below -3".

## PS2: the system list showed its name as plain text — fixed in 1.3

nes-box, the default theme, has no ps2 folder, so ES wrote "PlayStation 2" in
its plain font where the other systems have a logo and a picture. Ours goes in
as the 3ds one does (both of the image's copies of the theme, and by the OTA
hook `1.3-nesbox-folders.sh` into an installed one). The logo is
`PlayStation_2_logo.svg` from Wikimedia Commons in white, as nes-box draws the
PSP's; the picture is Kratos cut out of a God of War II wallpaper the owner
chose, without the flames around the blade, which did not come away from the
fog. Checked on the device on 2026-10-21, in the system list and above the
game list.

## Apple II in linapple did not start — in 1.1 and before, fixed in 1.2

linapple, the default emulator for Apple II, needs `libSDL_image-1.2.so.0`. In
the image that was a link to itself (`/usr/lib/libSDL_image-1.2.so.0` ->
`/lib/libSDL_image-1.2.so.0`, and `/lib` is `usr/lib`), so every game went
back to ES with

    error while loading shared libraries: libSDL_image-1.2.so.0: cannot open shared object file: Error 40

Error 40 is ELOOP. `cleanup_filesystem.sh` meant to point the link at the
library's real file with `$(find /lib/ -name libSDL_image-1.2.so.0.* ...)`, but
inside double quotes the `$(...)` ran on the build host, not in the chroot,
found nothing there, and `ln` got only the link name. Upstream has the same
line. It now runs in the chroot and makes `libSDL_image-1.2.so.0 ->
libSDL_image-1.2.so.0.8.4`, the link a plain `ldconfig` makes.

Found on 2026-10-18 by comparing a card updated by OTA with a fresh image. On
that card `apt install` had run a plain `ldconfig`, which fixed the link, so a
card where anything was installed with apt already works. With the image's
link put back there, linapple failed as above. The 1.1 → 1.2 package carries
the link; cards that ldconfig fixed already have it and skip it. In 1.2 the
Apple II system starts linapple (its own Master.dsk, 2026-10-18).

## Sleep emptied the battery in a day, the black screensaver in ten hours — fixed in 1.3

A report on 1.0: the battery ran flat "in sleep". Measured on revision B on
2026-10-19, on battery, with the RK817's coulomb counter (Q_PRESS, registers
0x74-0x77; the driver's own `cap` before and after a suspend agrees):

| state                                            | current     | 4654 mAh last |
|--------------------------------------------------|-------------|---------------|
| ES, backlight 200                                | ~700 mA     | ~6.5 h        |
| ES with the "black" screensaver (backlight 0)    | ~450-470 mA | ~10 h         |
| no ES, backlight 0                               | ~315 mA     |               |
| suspended, s2idle (`freeze`, up to 1.2)          | ~185 mA     | ~25 h         |
| suspended, deep (`mem`)                          | ~42 mA      | ~4.5 days     |

* **Sleep was s2idle.** `finishing_touches-rk3562.sh` set
  `SuspendState=freeze`, as the RK3566 scripts do, though the kernel offers
  `s2idle [deep]`. In s2idle the cores only idle and the SoC and the PMIC stay
  up. Deep sleep goes through ATF, with the cores, VOP, DSI and the SDIO Wi-Fi
  off. It is `mem` now. Seven wakes from deep sleep by the RTC and three by the
  power key: picture, sound, Wi-Fi and Bluetooth came back every time.
  `SuspendState=` is a list, and a drop-in with only `SuspendState=mem` adds
  to `freeze` (systemd takes the first state the kernel has, and that is still
  s2idle); a drop-in has to clear it with an empty `SuspendState=` first.
* **The "black" screensaver is not sleep.** It only turns the backlight off:
  the device runs on at ~450 mA and looks exactly like it sleeps.
  `screensaver-autosleep` (`scripts/rk3562/screensaver-autosleep.sh`), started
  by ES's sleep event and stopped by its wake event
  (`Emulationstation/scripts.rk3562`), suspends the device when a screensaver,
  any of them, has run for `ScreenSaverTime` again on battery, so twice the
  timeout after the last input; on the charger the count starts over. The
  power key then wakes the device into ES as it was: ES takes the press and
  ends the screensaver. A wake without a press (the RTC alarm) leaves the
  black screensaver dark, since the sleep hook
  does not light the backlight while the screensaver's mark says `on`, and the
  device goes back to sleep after one more timeout. Seen on the device with a
  one-minute timeout: asleep at 2 min, by the alarm awake with the screen dark,
  asleep again a minute later; a 3 h 40 min sleep, then the power key.
* **Two hours asleep on battery, then off.** Even deep sleep empties the
  battery in days, so the sleep hook (`scripts/sleep.rk3562`) sets the RTC
  alarm on the way down, two hours on, and when that alarm wakes the device
  on battery it powers off; a game left suspended is lost. On the charger it
  sleeps on for another two hours. A power key press clears the time.
* **The cable woke the device.** Plugging the charger in or pulling it out
  wakes it from deep sleep: "Resume caused by IRQ 71, rk817" and "IRQ 96,
  husb311" (the USB-C controller), where the power key and the RTC alarm are
  rk817 alone. The hook then lit the screen, and on the charger nothing put
  the device back to sleep. Now, with husb311 among the causes, it goes back
  to sleep with the screen dark, and the time to the power-off runs on. Once,
  with the black screensaver on, ES ended the screensaver itself right after
  such a wake; the next time, under an input logger, no event came and the
  screen stayed dark.
* **logind refuses a power-off or a suspend from the hook**: "Call to
  PowerOff failed: Action suspend already in progress". Both are started from
  a transient unit that waits for the wake to end and retries for 30 s.
  Seen on the device with the time cut to 90 s: asleep, the cable in (awake,
  asleep again), the cable out (the same), off at 90 s, a short press booted
  through U-Boot. With the cable left in, the alarm put it back to sleep every
  two minutes, seven times, the screen dark.
* **The charger plugged into a device that is off** starts U-Boot's charge
  screen (`charge-animation`, `rockchip,uboot-charge-on`), not the system: the
  screen goes dark by itself after 15 s, a short press shows it again, a long
  press boots, and with the cable out U-Boot powers the device off; a short
  press then boots as usual. All four seen on the device on 2026-10-19.
* **What deep sleep still draws** is unexplained. Not the radios (wlan and bt
  soft-blocked: the same 42 mA), not the `rockchip-suspend` node (disabled in
  the vendor DT as well; enabled with the vendor's `sleep-mode-config` 0x5e2,
  the same). Every suspend also costs about 300 µAh for the way down and up.
* **"Quit → Shutdown System" powers off.** The report said the battery was
  flat after that too, maybe. ES runs `sudo shutdown -h now`; systemd gets to
  "System Power Off" in about 3 s, and the kernel sets DEV_OFF in the RK817:
  screen and LEDs dark, a short press boots through U-Boot, the battery the
  same 64 % before and after. The same path since the first kernel, so in 1.0
  too. The menu item does not go through `finish.sh`, whose
  `.SWAPPOWERANDSUSPEND` branch ("Switch power button tap to off") suspends
  instead. In 1.3 ES's `shutdown` event, on "yes", also starts
  `darkos-poweroff` in a unit of its own: it waits up to a minute for ES to
  end, then asks for the power-off every second until it happens, so ES dying
  on its way out no longer brings the menu back (TODO.md, "Open questions on
  the device").
* **ES restarted under the black screensaver** (a crash, `systemctl restart`)
  left the backlight at 0: the screensaver's mark in `/run/darkos-screensaver`
  still said `on`, and nothing lit the screen again. Since 1.3 the ES launcher
  (`emulationstation.sh.rg52mini`) finds the mark of an ES that is gone, clears
  it and the autosleep mark, and puts back the brightness from before the
  screensaver (60 if it has none).

## Nintendo 3DS: Azahar in RetroArch (1.3)

The 3DS system runs `azahar_libretro` in RetroArch: Azahar 2126.1.2 (the
libretro build of Citra's successor) with GammaOS's azahar-nano changes on top,
then our patches `azahar-patch-001` to `-018` (`rk3562_core_builds/patches/`;
009 and 015 are later azahar-nano commits). Its folder is `/roms/3ds`; on a
dual-boot card GammaOS's `n3ds` is an alias of it. Encrypted dumps do not run.

- **azahar-nano** is TheGammaSqueeze's fork for GammaOS: about 130 commits of
  speed work for Mali GPUs and in-order cores, such as fastmem and dispatch
  work in the arm64 JIT, a GPU thread (`AzaharGpu`) that runs the PICA command
  lists beside the emulation thread, and no GPU wait on a repeated frame
  (`azahar-patch-000`, from GammaOSNextDistribution-14 8d68e5ecaab). The 1.3
  test build shipped plain 2126.2. In Super Mario 3D Land with the CPU capped
  at 1008 MHz that ran at 24 fps, this core at 30 (half the game's speed: 30
  was then the GPU's limit, see "GammaOS's settings" below); at 2 GHz its
  emulation thread is 29% busy against 54%.
- **Its switches** (about 90; Android properties
  `persist.gammaos.azahar_nano.<name>` on GammaOS) are environment variables
  here, named the same with dots as underscores:
  `persist_gammaos_azahar_nano_fastmem=0` turns fastmem off
  (`azahar-patch-002`). Unset, each keeps its default, which for most is on
  when the GPU is a Mali (`/dev/mali0`), as on Android.
- **Vulkan, not GL.** On RetroArch's gl driver (GLES here) the Mali driver
  links each new shader in `glLinkProgram` on the emulation thread, seconds at
  a time: Super Mario 3D Land stood on its title. A per-core override
  (`~/.config/retroarch/config/Azahar/Azahar.cfg`) puts RetroArch on vulkan for
  this core, where Azahar builds its pipelines on worker threads.
- **Layout** defaults to "Large Screen, Small Screen" (patch on the core's
  default), which fills the 16:9 screen better than the screens stacked.
- **The Back key swaps the screens** (`azahar-patch-011`). The core swapped
  them on L3, the switch under the left stick, and a hard push of the stick
  closes it by itself: two key logs taken while playing caught L3 down for
  0.7 s and for 3 s. Back (BTN_TRIGGER_HAPPY1) is used by nothing else. The
  RetroPad has no button for it, so the core reads it from the pad's event
  device. Checked on the device with Super Mario 3D Land: Back swaps and swaps
  back, L3 does nothing, Select still pauses (TODO.md, 2026-10-24).
- **Speed: PGO.** Built with clang and a profile from playing Super Mario 3D
  Land on this device with an instrumented build of the same recipe, merged
  with GammaOS's own profile for azahar-nano (six games). Theirs alone did
  nothing here: in 1-1 with the CPU capped at 816 MHz the core ran at 24.7 fps
  without a profile, 24.8 with theirs and 30, the GPU's limit then, with the
  merged one.
  Theirs comes from an Android build (libc++, the NDK's clang): 44% of its
  counts are on functions whose code differs in ours, and clang drops them.
- **Frame rate.** The core hands RetroArch a frame on every emulated refresh
  of the 3DS, so the flips counted are emulated refreshes and 60 is full
  speed, as GammaOS's azahar-nano app counts them. The 30 a second in 1-1
  measured until 2026-10-22 was half speed (the game's sound ran at half speed
  too); this bullet used to take it for the game's own rate.
- **Speed: GammaOS's settings (2026-10-23).** Super Mario 3D Land ran at half
  speed here and at full speed in azahar-nano on GammaOS, on the same device.
  Both were measured from one save state with a Mali job trace and the core's
  own GPU timer (GammaOS's swap tracer, Android-only in the sources; since
  `azahar-patch-018` it runs only on request, below). The GPU
  took 23.8 ms a frame here (vertex and tiler 19.5, fragment 19.9 with both
  eyes; clears alone 6.6), against GammaOS's 15.2 with both eyes and 7.9
  without the right one. Clocks are the same in both (GPU 900 MHz, DDR
  928 MHz), and so is the work once the core is set up as there:
  - `azahar-patch-006`: accurate multiplication off by default (GammaOS's
    config default); asynchronous shader builds and "Disable Right Eye
    Render" as core options, both on, as GammaOS's frontend seeds them.
  - `azahar-patch-007`: no geometry shaders on Arm's driver (Android's build
    has none: "extremely expensive on tilers"), and what GammaOS's frontend
    turns on at every launch: vertex depth, render pass and folded clears,
    the hardware scissor, the JIT warm-up.
  - `azahar-patch-008`: the large-screen layout drawn at 1280x640, the
    panel's size, instead of 1920x960 (the core's present pass 2.45 ms, now
    1.21).

  | Super Mario 3D Land 1-1, standing | fps | GPU per frame |
  |---|---|---|
  | before | 32.4 | 23.8 ms |
  | 006 + 007, both eyes | 41.8 | 15.7 ms |
  | 006 + 007 (right eye off) | 60.0 | 9.9 ms |
  | 006-008 | 60.0 | 8.7 ms |
  | GammaOS (right eye off / on) | 59.4 / 50-58 | 7.9 / 15.2 ms |

  The rotate layer's pass costs about 0.3 ms a frame. With both eyes the GPU
  is 64% busy here and the game still runs at 42: the core waits about 15 ms
  a frame for RetroArch's present, where GammaOS presents on a thread of its
  own (TODO).
- **Core options saved before 006** keep accurate multiplication on: RetroArch
  writes every option of a core into `retroarch-core-options.cfg` with its
  value at the time. Images before it had no 3DS core in a release.
- **Games that draw the two eyes interleaved (2026-10-25, patches 013 and
  014).** Some games draw each object into one eye and then into the other:
  Castlevania: Lords of Shadow – Mirror of Fate here, Metroid: Samus Returns
  on GammaOS. "Disable Right Eye Render" does not catch that, and on the Mali
  GPU every switch between the eyes ends a render pass: Castlevania's intro
  ran at 9 fps. Two of GammaOS's switches help, and they are core options
  (Graphics):
  - **Interleaved Right Eye Skip** (`citra_stereo_skip`; GammaOS's
    `stereo_skip`) learns the buffer the game shows as the right eye and
    leaves out what is drawn into it and copied out of it. It works only with
    "Disable Right Eye Render" on and 3D off. The risk: a game that also uses
    that buffer for something it shows loses effects, or its top screen
    stops.
  - **GPU Flush Lag** (`citra_gpu_flush_lag`; `async_gpu_flush_lag`): when
    the game flushes its cache, the GPU thread may stay up to this many jobs
    behind instead of finishing all of them first. The risk: the game reads a
    picture the GPU has not finished, or changes data the GPU has not used
    yet (flicker, garbage). It affects any game, not only stereo ones.

  Both default to **Auto**: on (the skip, a lag of 24) for the titles listed
  in the core (`title_switches` in `core_settings.cpp`; now only
  000400000009E500, the owner's Russian copy of Castlevania), off for the rest,
  as GammaOS has them. Castlevania's intro runs at about 21 fps with Auto (20.3-21.2
  in 1.3), 9 with both off. They are not on for every game because only that one was tried.
  A change applies at once. To keep it for one game: Quick Menu → Core
  Options → Manage Core Options → Save Game Options. At Auto, the environment
  variables of patch 002 (`persist_gammaos_azahar_nano_stereo_skip`,
  `persist_gammaos_azahar_nano_async_gpu_flush_lag`) still win over the list.
- **Realtime Audio** (Audio, patch 016; Azahar's `enable_realtime_audio`):
  when a game runs below full speed, its sound plays at real time instead of
  slowing down with it, so it does not break up, but it can run ahead of the
  picture. On by default, as in GammaOS: in Castlevania's intro (21 fps) the
  owner found the sound much better with it.
- **Cheats (2026-10-25, patch 017).** Quick Menu → Cheats lists the game's
  cheats; switch them and Apply Changes. They come from a folder shared with
  GammaOS, `ROMs/cheats/3ds/azahar` (`/roms/cheats/3ds/azahar`), laid out as
  GammaOS's azahar-nano keeps it:
  - `<title id>.txt`: every cheat of the game with its `*citra_enabled`
    mark. Azahar's cheats folder points here, so its cheat engine loads the
    list when the game boots and saves the marks to it. On a dual-boot card
    a cheat switched on in one system is on in the other.
  - `custom/<title id>.txt`: the user's own cheats, in the Gateway / CTRPF
    text format (a README there shows it). A code typed into RetroArch's
    cheat menu lands here as "RetroArch code N".

  The database is GammaOS's `cheats3ds.zip` (CTRPF, 60FPS and Sharkive
  Action Replay codes, 752 games), unpacked into `/usr/share/azahar/cheats3ds`
  by `build_retroarch.sh`, so a card with dArkOS alone has it too. Before a
  game boots the core rebuilds its list from the user's cheats, then the
  database's, the marks carried over by name, and writes the same list as
  RetroArch's cheat file (`~/.config/retroarch/cheats/Azahar/<rom>.cht`),
  which RetroArch reads right after loading the game. Checked on the console
  with Super Mario 3D Land (Europe): 12 cheats listed, one switched on and
  off through RetroArch, the mark in the shared list each time. Castlevania:
  Lords of Shadow – Mirror of Fate has none in the database.
- **Offscreen draws are never left out** (`azahar-patch-015`, GammaOS's
  e842af2339c). A draw into a texture the game composes once (Pokemon Sun's
  skins) was left out while its pipeline compiled, with a cold cache, as the
  bottom screen's draws were before patch 009. On the console with it:
  Castlevania 21.1 / 20.6 fps, Super Mario 3D Land 59.9, as before.
- **GammaOS's GPU timer only on request** (`azahar-patch-018`). azahar-nano
  put GPU timestamps around every render pass, clear, copy and present in
  every game, while only a profiling session reads them. It now starts only
  with `persist_gammaos_azahar_nano_gputime=1` in the environment.
  Castlevania's intro: 50.1 ms of GPU a frame before, 48.7 after on libmali;
  64.0 and 63.1 on malisx2 (TODO.md, "Where Castlevania's GPU time goes on
  malisx2").
- **A progress bar while the shader cache loads** (`azahar-patch-027`,
  2026-10-28). A game whose cache another build made compiles all its
  pipelines again at start (Castlevania 626, about a minute), and the core did
  it in `context_reset`, where RetroArch presents nothing: a black screen that
  looked like a hang. The load now runs on a thread from the first
  `retro_run`, which meanwhile presents RetroArch's progress message
  ("Compiling shaders: n / m"), shown only once the load has taken a second.
  A savestate RetroArch loads meanwhile (the entry state from ES's "-e") is
  kept and loaded when the cache is done: a first build waited for the cache
  in RetroArch's undo copy before it, on RetroArch's thread, and the screen
  was black again. Checked on the console with Castlevania and an empty
  pipeline cache, without a savestate and from slot 0: the bar went 17, 147,
  325 of 626 at 8, 25 and 45 s, the game ran at 75 s.
- **Internal resolution x2 (2026-10-29).** Users of GammaOS's azahar-nano
  said Super Mario 3D Land plays well at x2. Measured here from one savestate
  (Boom Boom's room, the 1.4 core, two rounds of 30 s; 60 is full speed):

  | `citra_resolution_factor` | libmali | malisx2 7732b3a |
  |---|---|---|
  | 1 (the default) | 60.2 | 60.2 |
  | 2 | 59.8 (11 frames of 1800 late) | 40.2 |
  | 3 | 30.1 | 25.6 |

  At x1 malisx2 takes less CPU for the same 60 (RetroArch's process 69%
  against 82%); at x2 the GPU decides, and Arm's driver draws it faster.
  Screenshots of the same emulated frames (RetroArch's `--max-frames-ss`) at
  x1, x2 and x3 show the same scene, only smoother: the dark shape under a
  shadow that falls across the platform's edge is there at x1 too. Heavier
  games (Castlevania at 21 fps, Luigi's Mansion at 49) are bound by the GPU
  already at x1, so x2 is not a default for all; a light game can be set to
  x2 in its own core options (RetroArch's Quick Menu > Core Options > Game).
  No per-title x2 in the core, as Castlevania's stereo has (patch 013): the
  owner's call of 2026-10-29. x2 runs close to the limit, so heat and the
  scene decide whether it holds 60, and that is the user's choice to make.
- **The texture upload ring is 128 MiB, not 512** (`azahar-patch-028`,
  2026-10-29). The Vulkan renderer stages texture uploads through one ring of
  host-visible memory, and Mali's kernel driver backs all of it with RAM as
  soon as it is made, RAM that cannot go to swap. What goes through it is a
  guest texture at its native size (1024x1024 RGBA8, 4 MiB, at most; the
  internal resolution is scaled on the GPU afterwards) or a custom texture.
  Measured on the console on libmali, with a test core that took the ring's
  size from the environment, 50 s from loading (savestates; Luigi's Mansion
  from boot): RetroArch's peak RSS, which counts the driver's mappings, and
  the GPU's memory, in MB:

  | ring | Super Mario 3D Land | Castlevania | Luigi's Mansion |
  |---|---|---|---|
  | 512 MiB | 1353 / 689 | 1386 / 737 | 1067 / 688 |
  | 128 MiB | 1009 / 305 | 1044 / 352 | 653 / 304 |
  | 64 MiB | 945 / 241 | 978 / 288 | 588 / 239 |

  In none of these runs did the ring fill up and start over, at 64 MiB
  either, so no upload waited for the GPU. 128 MiB keeps room for a
  4096x4096 RGBA8 custom texture (64 MiB); a single upload larger than the
  whole ring would stop the core. With 1 GB of RAM (malisx2's `balloon.py`
  locking 1024 MB, which leaves about what such a device has; ES stopped;
  zram and the SD's swap as the image sets them up):
  - 512 MiB: Super Mario 3D Land's own memory went to swap while it loaded,
    and the console stopped answering for 25 minutes, until the kernel's OOM
    killer ended RetroArch;
  - 128 MiB: Super Mario 3D Land 60.3 fps, Castlevania 21.2, as with 2 GB,
    with up to 240 MB of swap in use; with zram alone Castlevania 21.4; with
    the SD's 256 MB of swap alone the OOM killer ended Castlevania while it
    loaded;
  - 128 MiB and 140 MB more locked, for ES's own memory, which stays while a
    game runs: Castlevania 19.75 fps, frames up to 350 ms apart, 310 MB of
    swap.

  So a 1 GB device runs these 3DS games with the 128 MiB ring as long as
  zram is on.
- **Azahar's colour targets are compressed (AFBC)** (`azahar-patch-029`,
  2026-10-30). GammaOS's `rt_compress` (patch 000) makes an RGBA8 surface
  that no shadow pass has used without MUTABLE_FORMAT, so that the driver can
  compress it, and makes it anew with storage usage once a shadow pass needs
  one. But the format's traits carried storage usage for every RGBA8 image
  anyway, and libmali compresses no image with storage usage: the targets
  stayed uncompressed. Found by the libmali session; 029 leaves storage usage
  out of such surfaces. With it, Castlevania's 240x400 and 240x320 targets
  are AFBC in libmali's memory dump (U-Interleaved before); the surfaces that
  took the shadow path stay uncompressed, as they must. libmali's layer, which
  strips storage usage from such images (`VKT_RT_NOSTORAGE=1`), finds none
  left on 029 in Super Mario 3D Land, Castlevania and both Luigi's Mansions.
  No GPU faults; the pictures as before. Fragment work a frame against the
  028 core (libmali's GPU trace, frames 360-660 from the owner's states, two
  runs each):

  | | 028 | 029 |
  |---|---|---|
  | Castlevania | 36.6 / 36.1 ms | 36.0 / 35.4 ms |
  | Luigi's Mansion 2 | 73.1 / 73.7 ms | 73.9 / 73.7 ms |
  | Super Mario 3D Land | 5.4 ms | 5.1 ms |

  The frame rate is the same within noise: compression saves memory traffic
  where the targets are read and written a lot, and Luigi's Mansion 2 is not
  held back by that. At internal resolution x2 and above, `ScaleUp` still
  makes the scaled image with storage usage and MUTABLE_FORMAT, so it stays
  uncompressed; not changed, since x1 is the default.
- **RetroArch's FPS counter costs about 20%** on the vulkan driver (29.7 fps
  without it, 24.3 with it, same savestate, 2126.2). Measure without it.
- **RGA scaling** (`video_ctx_scaling`) changes nothing on the vulkan driver
  (29.8 against 29.9), so it stays off.
- **Save states in RetroArch take 1.4 s instead of about 27**
  (`azahar-patch-012`, streamed through zstd; `retroarch-patch-0024` skips
  RetroArch's size check on a load; TODO.md, "Azahar: save states in
  RetroArch").
- **Savestates from the 1.3 test build** (2126.2) load and play. Azahar
  refuses a state made by another source revision; `azahar-patch-005` makes
  that a warning in the log.
- A savestate made in New 3DS mode (the core's default) does not load in Old
  3DS mode: RetroArch quits.
- The core keeps its NAND and SD card in an `Azahar` folder next to the games,
  as RetroArch keeps saves next to them here.
- Theme: nes-box has no 3ds folder, and ours goes into the theme (both of the
  image's copies, and by an OTA hook into an installed one). Its logo is
  Nintendo's own 3DS logo in nes-box's white and red; the picture is Samus from
  Nintendo's press render for Metroid: Samus Returns.

## Keys: L3 closes by itself, Home toggled Bluetooth — fixed in 1.3

The case names two buttons the code does not: Home is BTN_MODE (SDL's button
10, RetroArch's hotkey enable), Back is BTN_TRIGGER_HAPPY1 (SDL's button 17),
which nothing used. L3 is the switch under the left stick, and a hard push of
the stick closes it by itself: two key logs taken while the owner played
caught it down for 0.7 s and for 3 s (2026-10-24).

- **Azahar** swapped its screens on L3. It swaps them on Back now (above).
- **DraStic** opened its menu on L3. `drastic/hotkey`, an LD_PRELOAD library
  the launchers load when it is there, puts the menu on Home+X, the
  combination that opens RetroArch's menu. Home+X reaches DraStic as L3,
  still the menu's key in `drastic.cfg`; a real L3 is dropped; while Home is
  held no button reaches the game; Home alone still swaps the screens.
  Checked on the device with Diddy Kong Racing DS.
- **Home toggled Bluetooth.** ogage's rg503 branch runs `bttoggle.sh` at every
  BTN_MODE press, so each Home+X for RetroArch's menu turned Bluetooth on or
  off: about 15 toggles in the journal of half an hour of play.
  `ogage/patches/0001` removes it. Bluetooth is switched in Options >
  Bluetooth.

## GTA San Andreas: the camera turned by itself — fixed in 1.3.1

The GTA San Andreas port (folder `gtasa`, a wrapper around the Android game;
seen in its builds v15 and v30) reads the pad through evdev itself and grabs
it before SDL starts. It picks a layout by the pad's name, does not know
`rk3562-joystick` ("unknown device — using R36S layout as fallback" in
`gtasa.log`), and in that layout writes ABS_Z into the right stick's X and
ABS_RZ into its Y, where some R36S kernels report the right stick, taking
every axis's rest as (min + max) / 2. Here ABS_Z/ABS_RZ are L2/R2, 0..32767
and 0 at rest: after any event of L2 the right stick sits at full left (0
against 16383) and the camera turns until the right stick is moved; R2 does
it to the vertical. A press does it, and so does the trigger's noise: L2 rose
to 1000-1300, past the kernel's dead zone of 800, a few times in 30 s with
nobody pressing it. The vendor's kernel reports the triggers on the same axes
(`play_joystick`, 0..9, 0 at rest), and by the port's code it takes the same
branch there.

`trigaxes-off` (`trigaxes-off/`, built by `build_trigaxesoff.sh`), which
`runport.sh` preloads for a port script naming a `gtasa` folder, drops
ABS_Z/ABS_RZ from the process's reads of the pad; L2/R2 still reach the port
as BTN_TL2/BTN_TR2, which it binds them to. On the console with v30: the
camera stays, L2/R2 work (2026-10-26).

GTA III and Vice City (re3/reVC from PortMaster) read the pad through SDL's
game controller, which keeps the triggers apart from the right stick; a
camera turning there has another cause, not looked into (their stick dead
zones are 0 in `re3.ini`/`reVC.ini`).

## HDMI output does nothing

Reported from the device on 2026-10-01: plugging a cable produces no reaction
at all. Not being worked on yet; recorded so the next session does not start
from scratch — or from the wrong guess, which is what happened here first.

**The kernel is not the problem, and this was checked rather than assumed.**
HDMI on this board does not come from the SoC. The panel is DSI, and HDMI comes
off an **RK628 RGB-to-HDMI bridge** — `rk628@50` on i2c4 (`i2c@ffa30000`),
`rk628-rgb-in` and `rk628-hdmi-out`, reached through the `route-rgb` display
route rather than an HDMI one.

The same bridge, the same device tree node and the same kernel config drive
HDMI successfully in the Android image for this board, which settles it:

* `rk628@50` is byte-for-byte identical between our built DTB and the vendor's
  `BSP/rk3562-rg52mini.dtb` — same bus, address `0x50`, enable/reset/interrupt
  GPIOs, and `soc_24M` at 24 MHz.
* Every RK628 and display symbol is identical between this defconfig and
  `mamaich/kernel_rk3562_rg52mini`, where HDMI works: `RK628_MISC`,
  `RK628_MISC_HDMITX`, `VIDEO_RK628_CSI`, `VIDEO_RK628_BT1120`,
  `ROCKCHIP_RGB`, `DRM_ROCKCHIP`, `ROCKCHIP_DW_HDMI`, `ROCKCHIP_INNO_HDMI`.
* The `rockchip_rgb` deferred-probe fix is in both trees.

So the gap is on the dArkOS side, and the first guess — that the bridge fix had
traded HDMI away — is wrong. HARDWARE.md's line "Only HDMI through that bridge
is lost" describes what happens when the bridge is silent, not what happens
here.

**What is on the dArkOS side is `scripts/rk3562/hdmi-test.sh`,** which writes
`/var/run/drmConn` and `/var/run/drmMode` for EmulationStation and RetroArch.
It cannot work as written, for three independent reasons:

    HDMI_STATUS=$(cat /sys/class/drm/card0-HDMI-A-1/status 2>/dev/null)
    if [ "$HDMI_STATUS" == "connected" ]; then
        echo 1 | sudo tee /var/run/drmConn
    fi

1. It reads `card0-HDMI-A-1`. The output here is the RGB route through the
   RK628, so the DRM connector is most likely not called that — and `2>/dev/null`
   makes a missing path indistinguishable from a disconnected display.
2. It runs once, from `@reboot` in crontab. Nothing re-runs it on hotplug. The
   udev rule that does fire on DRM events (`99-hdmi-audio.rules`) calls
   `audio-switch.sh`, which handles audio only.
3. `drmConn=1` assumes the HDMI connector is index 1, which is a guess rather
   than something read from the card.

The kernel command line carries `video=HDMI-A-1:1280x720@60` and names the same
possibly non-existent connector.

Where to start is in TODO.

### Measured on the device, 2026-10-28: HDMI works, nothing follows the cable

Checked on a running rg52mini (kernel 5.10.226, dArkOS 1.3.1) with a cable
connected, over ssh. This corrects two of the three reasons above and confirms
the third.

* **The connector name is right.** `card0-HDMI-A-1` exists, reads `connected`,
  has a 256-byte EDID and a mode list starting at 1920x1080; extcon `4-0050`
  (the RK628) reports `HDMI=1`. So the hardcoded name, and with it
  `video=HDMI-A-1:1280x720@60` on the kernel command line, are fine (reason 1).
* **`drmConn=1` is the right index** (reason 3): EmulationStation started with
  it comes up on `HDMI-A-1`.
* **There is one CRTC.** VOP2 exposes only `video_port0`, shared by `DSI-1` and
  `HDMI-A-1`, so the panel and HDMI are never on at the same time, and the kernel
  logs `drm_client: HDMI connected, disabling internal panel` on a plug. Which
  one EmulationStation drives is decided only when it **starts**, from
  `/var/run/drmConn`.
* **The actual defect is reason 2.** With the cable connected: `drmConn=0` and
  an EmulationStation restart stays on the panel; `drmConn=1` and a restart gives
  a 1920x1080p60 picture through the RK628 (`rk628 4-0050: src 1920x1080
  clock:148500`). A cable present at boot works because `hdmi-test.sh` writes
  `1`. A cable plugged in later does nothing, and unplugging leaves
  EmulationStation on an output that no longer exists, so the panel does not come
  back. (Cable present at boot working, and the unplug behaviour, are the
  owner's observations; the two restarts above are what was run on the device.)

**Fix:** `scripts/rk3562/hdmi-hotplug.sh`, run by
`scripts/rk3562/99-hdmi-hotplug.rules` on every DRM change event (the same event
that already drives `audio-switch.sh`). It waits 2 s for the bridge, compares the
HDMI status with `drmConn`, and on a difference rewrites `drmConn` and restarts
EmulationStation. `finishing_touches-rk3562.sh` installs both. (doster00's pull
request #2, merged 2026-10-29.)

On top of it (2026-10-29, the owner had seen the cable bounce): a burst of events
from a wiggled cable is coalesced (one handler runs, at most one more waits and
reads the cable afresh), and a new state is acted on only when it holds for a
second more (read five times). EmulationStation stays alive while a game runs
(the game is its child), so a restart ends the game with its unsaved progress:
- **Plugged in during a game:** the game goes on on the panel, and the switch to
  HDMI waits until it ends (one waiter, which runs the handler again).
- **Unplugged during a game on HDMI:** the game is ended with the restart, after
  the debounce. Waiting instead was tried in the pull request and leaves a black
  screen until the game is quit, because a game drawing on HDMI cannot move to
  the panel.
- **Games started from Ports** are not children of EmulationStation: their
  launchers run them in a systemd unit of their own (`NAME-port-UID`, from
  `systemd-run`), so restarting it did not end them. Left alone, such a game
  kept drawing to the output that was gone and held the display lock, and the
  next launch closed at once (seen on an RG52 Mini with Cuphead). They count as a
  game for the two cases above, and when the cable is pulled the handler stops the
  running `*-port-*.service` units.

**Found while testing the unplug:** switching to HDMI also leaves the internal
backlight at 0 (`/sys/class/backlight/backlight/brightness`), and nothing puts it
back, so after the switch to the panel the screen was black although
EmulationStation and the DSI link were up. The last non-zero value is kept by
`99-darkos-brightness.rules` in `/run/darkos-brightness`; `hdmi-hotplug.sh`
restores it when it switches back to the panel.

**Sound over HDMI had never worked** (doster00's pull request #3 and kernel
patch, 2026-10-30). The RK628's sound card (`rockchip,hdmi-rk628`, on SAI1) did
not probe: our device tree gave sai1 the `i2s1m0` pins, which the RGB output to
the RK628 holds as video data lines, so the kernel refused them (`pin gpio3-22
already requested by ff060000.syscon:rgb; cannot claim for ff810000.sai`) and
`rk628-sound` stayed deferred. The vendor's device tree (from the eMMC, v10 and
v14) puts sai1 on `i2s1m1`, GPIO3_B2-B5, which nothing else here uses, and so
does kernel eba0d6e. With it the card is there on the console (`card 1:
rockchiphdmirk6`; the speaker stays card 0, the panel as before). Two faults on
our side went with it:
- `audio-switch.sh` pointed `~/.asoundrc` at the HDMI card whenever a cable
  was in, card or no card, and then there was no sound at all, not even on the
  speaker. It now switches only when the card is in `/proc/asound/cards`.
- `.asoundrchdmi` named the card `rockchiphdmirk628`, but ALSA cuts card ids to
  15 characters, so the card is `rockchiphdmirk6`.

**RetroArch's picture lay on its side on HDMI** (pull request #5): patch 0000
turns the frame 90 degrees for the portrait panel, whatever the output.
`retroarch-patch-0025` turns it only when the display libgo2 picked is
portrait. On a landscape one it reports the real size and presents the frame
unrotated, as 0008 does on the landscape units. SDL ports were right already,
since SDL rotates only for a DSI connector.

Not covered: the picture stays 1920x1080, so a port or emulator that wants the
panel's 720x1280 gets whatever it asks of the HDMI mode list; and a game on HDMI
is ended when the cable comes out, so its unsaved progress is lost. **Checked with a cable on 2026-10-30** (the owner moved it, an HDMI grabber on
the PC showed the picture and recorded the sound):
- ES switches once per plug.
- RetroArch's picture on HDMI is upright and fills the height.
- The sound plays over HDMI.
- A game on HDMI is ended when the cable is pulled, and ES comes back on the
  lit panel.
- A cable plugged in during a game waits for its end.
- The same two cases with Cuphead, a Port in a `cuphead-port-0` unit, started
  by ES.

**The cable moved in and out every few seconds** while ES was starting on HDMI
made ES exit at once, several times in a row (`rk628: failed to get edid`).
After 5 failures in 10 s systemd stopped restarting it (the start limit), and
the handler's restart for the pull that followed was refused, or skipped as ES
was not running: a black screen until a reboot. The handler now restarts ES when
it runs, is being restarted or has failed, after `systemctl reset-failed`; not
when it was stopped on purpose. Checked the same way: the panel came back.

Not covered: why ES exits when HDMI's EDID cannot be read. While the cable is
moved, it fails until the next change is acted on.

## Scraper: ScreenScraper's developer login (since 1.4)

ScreenScraper wants a developer login (devid, devpassword) in every request. ES
takes it at build time (`exports.sh`: DEV_ID, DEV_PASS; gitignored). The images
up to 1.3.1 were built without one, so every scrape failed with "Erreur de login :
Vérifier vos identifiants développeur".

`es-0003-screenscraper-dev-login.patch` decides at run time, in this order:
1. The user's own login: `ScreenScraperDevId` and `ScreenScraperDevPassword` in
   `~/.emulationstation/es_settings.cfg`.
2. The build's, if ScreenScraper takes it. It is asked once (`ssinfraInfos`),
   since a login in an image can go out of date. With no answer at all (no
   network) it is kept.
3. One fetched from a friendly project's server, whose address is in the
   patch. Its author asked that the source not be advertised, so release
   notes and these docs do not name it (the owner, 2026-10-31). It is fetched
   once per run while the server answers.

A check or fetch with no answer (curl, 5 s) is tried again a minute later, and
the login is kept in memory only. A user's ScreenScraper account (SCRAPER >
USERNAME, PASSWORD) is sent as before.

Checked on the console on 2026-10-31 with an ES built without a login: a single
game scraped through the fetched login, with cover, rating, developer and
description. The owner scraped on the 1.4 image the same day: it works.
The console's CA store verifies both servers. ScreenScraper's certificate
chains to Let's Encrypt YE2, which Python's certifi on the PC did not take.

## USB: ROMs from a PC over MTP (since 1.4)

On a cable to a PC the console shows up as a media device, as an Android phone
does, and ROMs are copied with the PC's file manager. Windows needs no driver.
Asked for by the owner on 2026-10-31.

How it works:
- `umtp-responder` (Debian's 1.3.10) serves `/roms` as storage "ROMs", plus
  `/roms2` as "ROMs (SD2)" when the second card is mounted. It does so through
  a FunctionFS function of a USB gadget that `darkos-mtp.service`
  (`scripts/rk3562/darkos-mtp.sh`) builds in configfs.
- The kernel had all of this already (`CONFIG_USB_CONFIGFS_F_FS`). The USB-C
  controller (`fe500000.usb`, dwc3) is OTG behind a role switch and becomes a
  device on a cable to a PC.
- `umtprd` runs as ark, so copied files are ark's: group `media_rw` with the
  ROM folders' ACLs, umask 002. Hidden files are not shown.
- The PC sees the model from the device tree without its vendor ("RG52 Mini"),
  maker "dArkOS". The serial is `dArkOS-` plus the board's serial.
- On by default. Options > "Disable USB File Transfer" stops it, and it no longer
  starts at boot; "Enable USB File Transfer" brings it back.
- ES lists new games after SELECT > UPDATE GAMES LISTS in a game list.

Measured on the console on 2026-10-31, with a cable to a Windows PC:
- about 21 MB/s to the console, a 300 MB file with the right checksum;
- browsing the 220 ROM folders;
- the cable pulled during a copy, and the PC finds the console again once it is
  back in.

What it took:
- **`-conf <file>`**, not `-conf:<file>`. With the colon umtprd quietly took
  `/etc/umtprd/umtprd.conf`, the package's example (storage `/var/lib/umtp`,
  maker "Viveris Technologies"). Windows then kept that name for the serial, so
  the serial got the `dArkOS-` prefix.
- **A cable pulled during a copy** left umtprd in a FunctionFS read that no
  signal ends, a zombie even after SIGKILL: the port came back configured, and
  the PC got no answer. Only unbinding the gadget from the controller returns
  the read. So the service unbinds first when it stops (`ExecStop`), and
  `99-darkos-mtp.rules` restarts it at every USB disconnect (`USB_STATE=
  DISCONNECTED` from the kernel's `android_usb` device). It skips the restart
  within 5 s of its own start, as the gadget's own unbinding sends the same
  event.
- A copy cut off this way leaves the partial file on the console. MTP on
  Android does the same.
- While a big file is being copied, the PC's file manager shows the console
  empty: MTP serves one operation at a time.

**A USB device on the port with the gadget up** (2026-10-31, a USB-C flash
drive). The port turned host and source by itself, and the drive came up as
`sda`. The controller's UDC is gone while the port is a host, and the service
restarted once at the disconnect. With the PC's cable back, the port was a
device again, the gadget bound again (umtprd logged one read error and
carried on), and Windows listed the ROMs.

## Smaller things

* **No plymouth.** The command line carries `quiet splash` and
  `plymouth.ignore-serial-consoles`, but plymouth is not installed — only the
  leftover `text.plymouth` theme. The screen is simply black until
  EmulationStation starts. The boot logo (see HARDWARE.md) addresses this from
  the kernel side.
* **`enable_bluetooth.service` failed at boot** ("No default controller
  available") on revision B: the AIC8800 registers hci0 a few seconds into
  boot, after the script's `bluetoothctl power on`. bluetoothd powered the
  controller anyway, so only the unit's state was wrong. The script now waits
  up to 15 s for a controller and never fails the unit (1.2).
* **The PC-98 core could not load.** `np2kai` on christianhaitian's rg503
  branch since 2026-09-07 (NP2kai 5939e0c) leaves six `ymzadpcm_*` symbols
  undefined: "Failed to open libretro core". `build_retroarch.sh` takes the
  build before it (retroarch-cores e2a8f88be5), which loads on the device
  (1.2; found in llj503/dArkOS_ngp45ht).
* **fluidsynth runs all the time**, at 7-13 % of one core with nothing
  playing: Debian's `fluidsynth.service` user unit starts with ark's session
  and keeps a MIDI synth on the ALSA sequencer (client 128), with the sound
  device open. Nothing in the image was found to use that port - lzdoom links
  libfluidsynth itself - but a port that plays MIDI through ALSA would.
  Disabled for all users by `finishing_touches-rk3562.sh` in 1.2
  (`systemctl --global disable fluidsynth`); a port that needs it can start
  it with `systemctl --user start fluidsynth`.
* **ScummVM asks about "fluidsynth" in MIDI games** (fixed in 1.4, see "ScummVM
  opened every game on a dialog" above). Day of the Tentacle, for
  one, opens on "The preferred audio device 'fluidsynth' was not found ...
  Attempting to fall back to the next available device"; after OK the game
  runs on the fallback. `~/.config/scummvm/scummvm.ini` sets
  `gm_device=fluidsynth` and `mt32_device=fluidsynth` with
  `soundfont=.../Roland_SC-55.sf2`, but `/opt/scummvm/scummvm` is built
  without FluidSynth (no libfluidsynth linked, no plugin strings) and the
  SoundFont is not on the card. Not caused by the fluidsynth service above:
  ScummVM's device is its own driver. The same in 1.1, whose ScummVM and ini
  are identical (seen on the device on 2026-10-18).
* **A loud speaker on a nearly empty battery resets the console** (revision
  B, measured 2026-10-18 on battery only): the external amplifier's current
  peaks pull VSYS under the RK817's undervoltage threshold, and the PMIC
  restarts the board (ON_SOURCE 0xF5 = 0x10, RESTART_RESETB). With a
  full-scale signal and `DAC Playback Volume` at 252 (−1.1 dB, the most
  `rk817_dac_vol_put` accepts) it reset at 15 % after 20–40 s and at 11 %
  (3.49 V) within a second. dArkOS does not get there by itself: on a fresh
  card `alsactl init` sets the DAC to −20 dB (203), the value is stored and
  restored from then on, and ES and the volume keys only move the softvol
  `Master`. At 203, two 70 s runs at 15 % did not reset it. GammaOS hits it
  because Android puts the DAC at 252. Raising the DAC here needs a limit tied
  to the battery level; the GammaOS port is writing one for the rk817 codec.
* **A PortMaster port on a card without PortMaster** (a fresh card, the port
  copied over by hand) started without `control.txt` and `mod_dArkOS.txt`: no
  PortMaster controls, no E2BIG guard, no `LIBGL_GLES`, so Don't Starve's
  world was black again on the dual-boot card on 2026-10-19 although gl4es-fix
  was in the image. The image only ships the installer, in Tools. Since 1.3,
  `runport.sh` runs it before the first PortMaster port (with
  `~/no_es_restart`, or it restarts EmulationStation under the port), then
  `portmaster-e2big.sh`, then the port: seen on the device, the installer's
  output, then the game with its world, EmulationStation not restarted.
  Don't Starve's own shader fix (above) is in the game's files and has to be
  made again on a fresh copy of the port.
* **`rk3562-poweroff.service` failed at every shutdown**: "Failed to execute
  /usr/local/bin/rk3562-poweroff.sh: No such file or directory". Upstream's
  script has CRLF line ends, so `#!/bin/bash\r` is no interpreter. It is not
  installed any more (since 1.3): the kernel powers the PMIC off itself, after
  systemd-shutdown's last sync, and the script's i2cset would have cut the
  power before that sync.
* **N64 in RetroArch: the threaded renderer (1.3).** `mupen64plus_next` had no
  core options set. With `mupen64plus-ThreadedRenderer = "True"` in the
  image's core options AeroGauge (frames 600-1800) runs at about 57 fps
  instead of 54, Zelda's title (frames 1200-3000) at 57 instead of 52
  (TODO.md, 2026-10-24). An updated card gets the option from
  `ota/hooks/1.3-mupen64plus-threaded.sh` unless the user set it. N64 still
  starts `parallel_n64` in retroarch32 by default; mupen64plus_next is a
  choice in ES.
* **RetroArch with mupen64plus_next can outlive a SIGTERM.** On 2026-10-25
  `systemctl stop` of a transient unit running it (SIGTERM to the whole
  cgroup: the bash launcher, n64.sh and RetroArch) left RetroArch running,
  the picture frozen, until the 90 s stop timeout's SIGKILL, on libmali
  g29p1-4 and -5 alike. A SIGTERM to RetroArch alone ended it in 0.6 s
  (2026-10-24), and its own quit (the hotkey twice, `quit_press_twice`) in
  1.4 s. Why the two differ was not found; the whole cgroup against one
  process is the open question. retroarch32 with `parallel_n64` did the same
  once in a test harness (2026-10-18). Stopping ES (the power key's power-off,
  a shutdown, an update) sends SIGTERM to its whole cgroup, game included, so
  a power-off during an N64 game took a minute and a half. `TimeoutStopSec=10`
  in `emulationstation.service` (1.3) cuts that to 10 s.
* **`liblc3-0`, `libreadline7` "could not install"** — these are deliberate.
  `bluetooth_needed_packages.txt` lists the names from several Debian releases
  side by side, and the ones that do not exist in trixie are simply skipped.
  `liblc3-1` installs.
* **`libeatmydata.so` from LD_PRELOAD cannot be preloaded** — harmless, and
  the message says `ignored`. `cleanup_filesystem.sh` removes eatmydata while
  `LD_PRELOAD` still names it. The loader carries on with a real `fsync`.
