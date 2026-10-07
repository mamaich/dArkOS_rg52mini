# Known issues

All of these are visible in the build log. `make rg52mini` ends with an audit
that names them, so start there rather than reading 80 000 lines:

    ==================== COMPONENT AUDIT ====================
      EMPTY       /opt/ecwolf
      NO BINARY   /opt/gametank
      NO BINARY   /opt/hypseus-singe
      ---
      with binaries: 31   empty: 1   no executable: 2

      Build steps that gave up ...
      Packages that failed to install ...
    ========================================================

## Components that do not build

Three fail the same way: a patch in `rk3562_core_builds/patches/` no longer
applies to its upstream, `builds-alt.sh` stops, and the copy that follows
finds nothing.

| | |
|---|---|
| ECWolf | `ecwolf-patch-002-add-exit-menu.patch` |
| Hypseus Singe | `hypseussinge-patch-0001-buildfix.patch` |
| GameTank | `gametank-patch-001-disable-joystick.patch` |

Fixing them means working out what changed upstream and rewriting the hunks —
one job each, not a batch. They are all secondary emulators.

**Two of the three now fail without even trying.** The audit at the end of the
v09292026 build reported `/opt/ecwolf` empty and `/opt/gametank` and
`/opt/hypseus-singe` holding files with no ELF among them, and named the cause:
`Arkbuild_package_cache/rk3562/` carries `ecwolfsa.tar.gz` at 130 bytes and
`gametank.tar.gz` at 128 bytes. Those are empty tarballs written when the build
first failed, under cache keys that still match, so every later build restores
the failure rather than retrying it. Delete the two `.tar.gz` files with their
`.commit` partners before the next build or the patches above will never be
reached. Their two missing packages, `libfuse2` and `libpcap0.8`, do not exist
in trixie under those names either.

Yabasanshiro used to be a fourth, for a different reason, and is now building
again — see below.

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
reproducible. Four of the eleven patches apply; the other seven are skipped for
reasons rather than convenience, listed in the recipe. `YAB_WANT_VULKAN=OFF`,
because that fork has no `yabause/src/vulkan` at all — not because the device
lacks Vulkan, which it has. The `n2.cmake` toolchain file stays: it is the
aarch64 one, and `pi4.cmake` would build for armv7.

It compiles, first try, with no errors on the step: a 3.9 MB stripped aarch64
binary. Whether it runs on the device is untested.

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

## DSperate (NDS) is slow unless the governor is performance

DSperate, the second NDS emulator since v10062026, keeps about one CPU core
busy. Under the default `ondemand` governor that reads as light load, and the
CPU spends a third of its time at 1.4 GHz or below: the Pokemon Diamond &
Pearl demo runs at 41-47 fps with crackling sound, the 3D field of the Dragon
Quest Monsters Joker 2 demo at 48. Under `performance` (CPU 2016 MHz, GPU
900 MHz) both run at 59-60 fps with clean sound. Measured on the device on
2026-10-07; the figures are in TODO.

**Workaround:** in EmulationStation set the CPU governor of the NDS system,
or of the game, to `performance` when using DSperate. DraStic, the default NDS
emulator, is lighter and does not need it. The image is left as it is.

## SDL2 under KMSDRM re-raises every SIGSEGV: programs that handle their own crash

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
Nothing in the image sets the variable globally; the image is unchanged.

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

## The build cache is keyed by upstream version, not by build flags

Each component caches to `Arkbuild_package_cache/<chipset>/<name>.tar.gz` with a
`.commit` file holding the upstream tag or commit. That is the whole key.
Change a compiler flag, a patch, or anything else on our side and the key does
not move, so the cache is restored and the change does not happen.

Two consequences worth knowing:

* A flag change only lands in components that rebuild for some other reason.
  After the Cortex-A53 fix, everything restored from the 2026-09-15 cache was
  still tuned for the A55. Delete the relevant `.tar.gz` and `.commit` pair to
  force a rebuild.
* `build_yabasanshirosa.sh` is worse: it derives its key by curling the
  **upstream** christianhaitian script for its `TAG=`, not our fork's. Editing
  the tag here leaves the key unchanged and a stale tarball is restored over
  the new build.

A failed build used to be cached the same way — see below.

## Failures used to be cached like successes

Until 2026-09-16 a component that produced nothing still had its empty
`/opt/<name>` tarred up, 45 bytes, under a key that matched. Every later build
restored the failure instead of retrying it, and printed a line saying it was
using the cache. Six entries were in that state: kodi, bluealsa, freej2me-plus,
yabasanshiro, ecwolf, gametank. The build started to test Kodi would have
skipped Kodi.

`build_kodi.sh` now caches only when `/opt/kodi` holds an ELF, and the audit
lists every cache tarball under 20 KB, which covers the other thirty-odd
components without editing each script. The bad entries were moved to
`Arkbuild_package_cache/failed-2026-09-15/`.

If a component is mysteriously missing and the log says it came from the cache,
look at the size of its tarball first.

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

## Smaller things

* **No plymouth.** The command line carries `quiet splash` and
  `plymouth.ignore-serial-consoles`, but plymouth is not installed — only the
  leftover `text.plymouth` theme. The screen is simply black until
  EmulationStation starts. The boot logo (see HARDWARE.md) addresses this from
  the kernel side.
* **`liblc3-0`, `libreadline7` "could not install"** — these are deliberate.
  `bluetooth_needed_packages.txt` lists the names from several Debian releases
  side by side, and the ones that do not exist in trixie are simply skipped.
  `liblc3-1` installs.
* **`libeatmydata.so` from LD_PRELOAD cannot be preloaded** — harmless, and
  the message says `ignored`. `cleanup_filesystem.sh` removes eatmydata while
  `LD_PRELOAD` still names it. The loader carries on with a real `fsync`.
