# Performance

Four Cortex-A53 at 2.0 GHz and 2 GB of RAM in a case with no heatsink. Two
things actually limit emulation here — the thermal budget and memory pressure —
and everything below is sorted by whether it was measured, shipped, or merely
sounds good.

## What the device does under load

Measured on the Android port of the same hardware, which shares the SoC, the
case and the power tree:

    GPU  at 6/6 (the lowest of six DVFS steps) within seconds of a 3D load
    CPU  throttled by the same thermal zone
    skin temperature climbs until the trip points act, then stays there

The important part is that the GPU sits at the *bottom* step, not somewhere in
the middle. That rules out a whole family of explanations.

### Power-budget weights do not help — tested

The `power_allocator` governor divides the sustainable power budget between
cooling devices according to `contribution` in the device tree, exposed as
`cdev0_weight`/`cdev1_weight`. The obvious idea is to give the GPU more of the
budget and the CPU less.

It was tested at 8:1 — GPU 2048 against CPU 256 — and changed nothing. The GPU
stayed at 6/6.

Why: the constraint is not how the budget is split, it is the ceiling itself.
When the zone is above its target, the allocator shrinks *every* device's
allocation; the weights only decide the proportions of something that is
already too small. A bigger slice of a budget that is one sixth of what the GPU
wants is still the bottom DVFS step.

Do not re-litigate this one. It is written up in the Android port's
`docs/09-hardware-notes.md` under "what did not help".

### `sustainable-power`

`sustainable-power` in the thermal zone tells the allocator how many milliwatts
the device can dissipate indefinitely. It is a *description* of the hardware,
not a limit — raising it does not make the device cooler, it makes the governor
believe there is more headroom than there is, so it throttles later and less.

If the number is genuinely too low the result is a device that runs cool and
slow for no reason, and raising it is free performance. If it is right,
raising it means the die runs hotter, the trip points act harder when they
finally do, and the outcome is oscillation instead of a steady state — plus
whatever a permanently hotter part does to its lifetime and to the case
temperature in someone's hands.

Not changed. Without a thermocouple on the case there is no way to tell which
of those two situations this is, and the failure mode of guessing wrong is a
device that gets hot in the hand.

Raising `trip-point-1` has the same shape and the same objection. A heatsink or
a thermal pad against the shield can is the honest fix, and it moves the real
constraint instead of the reported one.

## Shipped

### Two-tier swap

2 GB of RAM with no swap at all was the previous state. Now:

| tier | size | priority | where |
|---|---|---|---|
| zram | 1536 MB | 100 | RAM, compressed |
| eMMC | 256 MB | 10 | `swap` partition |

zram fills first because its priority is higher; the eMMC partition only takes
what zram rejects — pages that do not compress, and pages cold enough to have
been written back. On Android on this device zram was clearly worth having, and
that is what prompted it.

`lzo-rle` is the compressor (`ALGO_PREF="lzo-rle lz4 zstd lzo"`, walked against
what `comp_algorithm` actually offers). Speed is the scarce resource on four
A53s: zram only pays if compressing costs less than the fault it avoids.
lzo-rle has been the kernel's own default since 5.1 for that reason. `lz4` was
added to the kernel (`CONFIG_CRYPTO_LZ4=y`) so the list has somewhere to fall.
Put `zstd` first instead if RAM turns out tighter than CPU — it compresses
appreciably better and would keep the eMMC tier idle longer.

Writing a name the kernel does not have fails *silently* and leaves the default
in place, which is how the first build ended up on lzo-rle by accident rather
than by choice. The script now reports which one it got.

eMMC wear was the concern, and the tuning answers it:

* `vm.swappiness=100` — sounds aggressive, is the opposite. It biases reclaim
  towards anonymous pages (zram, cheap) instead of file pages (re-read from
  eMMC). Low swappiness on a zram system means *more* eMMC traffic, not less.
* `vm.page-cluster=0` — one page per fault instead of eight. Readahead is
  pointless against RAM.
* The eMMC tier is last-resort by construction: priority 10 against 100.

The partition is found by GPT label, never hardcoded, and
`/usr/local/sbin/emmc-swap` refuses to touch it unless all four hold: the label
is `swap`, `blkid` says the type is already `swap`, the size is between 64 MB
and 2 GB, and nothing has it mounted. It waits up to 30 s for
`/dev/disk/by-partlabel/swap` to appear, because udev and the service race.
It never runs `mkswap` on a partition it did not already recognise as swap —
`mkswap` on the wrong device is unrecoverable.

`CONFIG_ZRAM_WRITEBACK=y` is in the kernel but not wired up. It is the other
way to spend the eMMC partition: instead of a second swap device, zram evicts
its own idle or incompressible pages to a backing block device. Cleaner in
principle — one swap device, the kernel decides — but it needs a writeback
device configured and an idle-marking policy, and the priority approach already
works and is trivially observable in `/proc/swaps`.

### Kernel modules stripped

`--strip-debug` on every `.ko` in `finishing_touches`: 100 MB → 6.3 MB. Not a
speed change, but it is 94 MB of an SD card and of every image download.

Never plain `strip` on a module — it removes the symbols `MODVERSIONS` CRCs are
computed against and the module stops loading.

### The kernel itself, not just its modules

`Image` went from 39,164,416 bytes in v09162026 to 32,965,120 in v09292026,
6.0 MB smaller, in two steps and for two different reasons.

The first, about 4.9 MB, was debugging the device never reads. `FUNCTION_TRACER`
alone was 473,376 bytes of `__start_mcount_loc` — 59,172 trace points, each of
which costs a 24-byte `dyn_ftrace` record at boot, about 1.4 MB of RAM, plus
the `-pg` prologue padding left in `.text` for good. `DYNAMIC_DEBUG` was
356,384 bytes of descriptors. Also off: `SLUB_DEBUG`, `SCHED_DEBUG`,
`IKCONFIG`, and the debugging inside the drivers that do run — `ROCKCHIP_DEBUG`,
`ROCKCHIP_DRM_DEBUG`, `ROCKCHIP_RGA_DEBUGGER`, `RK_DMABUF_DEBUG`,
`SW_SYNC_DEBUG`, `BT_DEBUGFS`, `USB_GADGET_DEBUG_FILES`, `PM_DEBUG`,
`DEBUG_DEVRES`, `BLK_DEV_IO_TRACE`, `RCU_TRACE`.

The second, 1.1 MB, was the two GPU drivers that cannot bind on this hardware —
see HARDWARE.md.

Deliberately kept: `KALLSYMS`, because panic reports now go to pstore and are
worth reading by name (`KALLSYMS_ALL` was already off); the hang detectors; and
`DEBUG_FS`, which turned out to need asking for explicitly — `BLK_DEV_IO_TRACE`
was the only thing selecting it, and `scripts/rk3562/ledctl.sh` drives the power
LED through `/sys/kernel/debug/regulator/vcc-led/enable`. Switching off
blktrace would have taken the LED with it. See BUILDING.md for how to catch
that class of mistake before it reaches an image.

### Cortex-A53 build flags

See KNOWN-ISSUES: sixteen core-build scripts targeted Cortex-A55, and the Amiga
emulator was built for ARMv8.2-A on an ARMv8.0-A part. `-mtune` was only
costing scheduling; the `-mcpu` was potentially fatal.

### PPSSPP without hardening

`-fno-stack-protector -U_FORTIFY_SOURCE` for PPSSPPSDL only, on the two
`-DCMAKE_{C,CXX}_FLAGS` arguments in `rk3562_core_builds/scripts/ppsspp.sh`.

They started out exported as `CFLAGS`/`CXXFLAGS` from `build_ppssppsa.sh`,
which did nothing at all: cmake reads those environment variables only when
`CMAKE_C_FLAGS`/`CMAKE_CXX_FLAGS` are unset, and that script sets both
explicitly. Nothing failed, nothing was logged — the build just quietly kept
the hardening. It was caught by reading `flags.make` out of a running build,
which is the way to check this kind of change:

    build/CMakeFiles/PPSSPPSDL.dir/flags.make

The reasoning is narrow and does not generalise: this device runs only software
from this image and never downloads executables, and PPSSPP's inner loops are
the kind of small hot functions where a canary per call is measurable. It has
not been benchmarked. If someone does measure it and the difference is noise,
take it back out — the flags exist for a reason.

### Dreamcast, Naomi and Atomiswave: the standalone flycast first

ES starts the first emulator of a system. Measured on 2026-10-17, build
20261016184025, overclock governor: each emulator's own FPS counter read from
four screen grabs 8 s apart, after 45 s.

| | Crazy Taxi (attract demo) | Knights of Valour - The Seven Spirits (Atomiswave) |
|---|---|---|
| standalone flycast | 59-60 | 59-60 |
| retroarch, flycast / flycast_rumble | 37-44 / 39-45 | 30-60 |
| retroarch32, flycast32_rumble | 38-45 | grey screen, 9.4 |
| retroarch32, flycast_xtreme | 42-48 | 30-60 |
| retroarch32, reicast_xtreme | 41-58 | 30-60 |
| retrorun, flycast | (VMU error page) 40-44 | 59-60 |
| retrorun32, the three 32-bit cores | (VMU error page) 36-46 | 55-60 |

The standalone is the only one at full speed in both, so it is first for all
three systems (Dreamcast changed in 98e7715; it used to start retroarch32
`flycast32_rumble`). A choice made in ES per system or per game still wins.
Saves do not carry over: the standalone keeps its own VMU and NVRAM files.

retrorun never got past Crazy Taxi's VMU error page because the test pressed
Start on a virtual pad (see KNOWN-ISSUES, retrorun), so its numbers there are
not comparable. Why `flycast32_rumble` (a May 2024 build from retroarch-cores)
shows a grey screen on this game was not looked into; it logs
"FlashRom: ReadAMDID1 unexpected write" where `flycast_xtreme` does not.

### A profile (PGO) for the heaviest emulators (1.3)

Compiler flags alone gained nothing on these emulators ("Not worth doing",
below); a profile did. Each is built with clang 19 and a profile trained on
this device, the compiler being xtool's amd64 clang in the chroot
(BUILDING.md):

| | build | measured | without | with |
|---|---|---|---|---|
| ARMSX2 (PS2) | profile, ThinLTO of the core | Jak and Daxter's PAL intro, frames 300-1850, fps | 31.4 | 34.1 (+8.6%) |
| Azahar (3DS) | our profile merged with GammaOS's | Super Mario 3D Land 1-1, CPU capped at 816 MHz, fps | 24.7 | 30, then the GPU's limit |
| malisx2 (Vulkan driver) | full LTO, profile | Castlevania's 368 pipelines compiled | 8.1 s | 7.4 s |

A profile names a file-local function by the path of its source, so it is
made from a tree at the chroot's path (`/home/ark/...`), or clang drops those
functions. GammaOS's profile alone did nothing for Azahar here (24.8 fps): it
comes from an Android build, and 44% of its counts are on functions whose code
differs in ours. TODO.md has the series.

### perfmax: performance unless told otherwise (1.3)

ES passes perfmax an empty governor unless one was picked for the system or
the game, and up to 1.2 games then ran on `ondemand`, `simple_ondemand` and
`dmc_ondemand`. perfmax now puts `performance` first. Super Mario 3D Land in
Azahar, one savestate, before `azahar-patch-006`: 24.0 fps before, 26.7 with
performance, 29.9 with overclock (KNOWN-ISSUES).

### gpu-cpucap: the CPU gives way when the GPU is held (1.3)

The CPU and the GPU share one thermal budget. Under the performance and
overclock governors perfmax starts `gpu-cpucap` and perfnorm stops it. Every
2 s the CPU's limit goes a step down while the GPU is 80% busy or more and the
thermal governor holds it, and a step up while the GPU is under 60% busy or the
SoC is 5 C under the first trip; 1200 MHz at the lowest. God of War's demo on a
hot device, on the charger, its first version (a 1608 MHz floor): 28.0 and
28.2 fps against 27.0 and 25.0 without. On a cool device it changes nothing
(TODO.md, "PS2: the GPU gets the heat").

### ARMSX2 reads its own target in the tile (1.3)

The PS2 template had `DisableFramebufferFetch = true` and
`OverrideTextureBarriers = 0`, set in June for the old PCSX2 fork. On ARMSX2
they force a copy of the target for every draw that reads it, which breaks the
render pass; at its defaults it reads the destination in the tile. God of War
from a savestate, the GPU held at 600 MHz and the CPU at 1608 MHz: 26.6 fps
against 25.2, with 15-27% less GPU work a frame. At the usual clocks on a hot
device the frame rate barely moves. The launcher puts an ini made from the old
template on the defaults once (TODO.md, 2026-10-23).

### N64 in RetroArch: the threaded renderer (1.3)

`mupen64plus_next` with `mupen64plus-ThreadedRenderer = "True"`: AeroGauge
(frames 600-1800) at 57 fps against 54 (KNOWN-ISSUES, smaller things).

### ES at rest: no brightnessctl twenty times a second (1.8)

ES's brightness popup asks for the level every 40 ms while ES is on screen,
and with brightnessctl installed every ask was a shell, brightnessctl and awk
through popen(). `es-0008` reads `/sys/class/backlight/backlight/` instead:
the code ES already had for a device without brightnessctl, which rounds the
percent as `brightnessctl -m` does. ES idle in the system list, ondemand,
30 s each, A B A B on 2026-11-08 (shares of one core):

| | ES | what ES started | whole system | CPU clock, mean |
|---|---|---|---|---|
| before | 22.5% | 19.8% | 56.5% | 774 MHz |
| es-0008 | 23.0% | 0.1% | 36.3% | 719 MHz |

Games are not affected, ES's loop stands still while one runs; the menu costs
a third less CPU. The whole system is `rq_cpu_time` of /proc/schedstat.
/proc/stat is no use here: it counts user and system time at the tick, which
misses most of a load that wakes 60 times a second for a few milliseconds,
and it put the whole system below ES alone.

## Two emulators were built with no optimisation at all

Worth more than everything else on this page put together, and it was found by
accident while wondering why one Dreamcast game was slow.

**flycast** asked cmake for `CMAKE_BUILD_TYPE=Release` and then set
`CMAKE_C_FLAGS_RELEASE` and `CMAKE_CXX_FLAGS_RELEASE` to `-DNDEBUG`. Those
variables *replace* what the build type carries rather than adding to it, and
cmake's Release is `-O3 -DNDEBUG` — so the `-O3` was thrown away and gcc fell
back to its default, `-O0`. The build log has `CMAKE_VERBOSE_MAKEFILE` on and
settles it: of 389 compile lines under `flycast-build`, exactly one carries an
optimisation flag. The SH4 memory handlers and the MMU translation, which are
the hot path for anything demanding, were all built unoptimised.

**fake-08** was the second one an audit of the same log found: 57 compiles, no
`-O` among them. That one is upstream's doing — the recipe builds the
`SDL2Desktop` target, whose makefile says `-g -Wall` and nothing more, because
it is the target meant for debugging on a PC. Every handheld target that
project ships — miyoomini, funkey-s, gcw0, bittboy — uses `-Ofast`.

Both fixed in `mamaich/rk3566_core_builds` (`ae544e5`, `6aaaa37`), which also
applies the `-march=armv8-a+crc -mtune=cortex-a53` correction from `7290d75`.

**Both are in v09292026**, and it was verified rather than assumed: with the
build running, `flags.make` under the PPSSPP build directory read

    -Ofast -fno-tree-slp-vectorize -D_NDEBUG -march=armv8-a+crc
    -mtune=cortex-a53 -ftree-vectorize -funsafe-math-optimizations
    -fno-stack-protector -U_FORTIFY_SOURCE -O3 -DNDEBUG

— the `-Ofast`, the correct `-mtune`, and the `-O3` that the build type carries
once nothing replaces it. The cache did not undo it either: with the key now
including the recipe hash, PPSSPP rebuilt from source instead of restoring the
artifact built under the old flags.

How to check this for any component, since nothing reports it:

    grep -c 'cc1plus\|cc1' build.log          # compile lines
    grep 'cc1plus\|cc1' build.log | grep -c '\-O[0-3sfast]'

A component whose second number is near zero is being built for debugging.

### Two more, found on 2026-10-27: libSDL2 and the ONScripter core

A static audit of the 1.3 image's machine code (the share of loads and stores
against the stack pointer, 4-13% in optimised code) found two more, which the
check above misses because their makefiles do not print compile lines:

* **libSDL2**, both the arm64 and the armhf copy.
  `rk3562_core_builds/scripts/sdl2.sh` gave cmake no build type, SDL's CMake
  picks none of its own, so there was no -O at all and SDL's debug asserts
  were in (`-- Install configuration: ""` in the build log; 35.8% stack
  operations against 10.9% in Debian's 2.32.4). Release since 1.3.1. It
  changes little: the emulators that run below full speed spend well under 1%
  of a core in SDL, which pumps events and swaps, and ES at rest took 14.9% of
  a core against 14.8% (2016 MHz fixed, two rounds each). The armhf copy rides
  in RetroArch32's cached tarball, whose key now hashes SDL2's recipe and
  patches as well.
* **The ONScripter core** (visual novels; rg503's download), -O0 since
  upstream replaced its Makefile with a CMakeLists that names no build type.
  `onscripter.sh` asks for Release and `build_retroarch.sh` builds it here
  since 1.3.1. On the device, a test script: 120 image changes (640x480 PNG
  and JPEG) in 2.6 s against 12.0, and 60 crossfades of 0.5 s in 32.4 s
  against 38.6 (30 s nominal: the -O0 core could not draw its fades in time).

## Not worth doing

* **LSE atomics** (`CONFIG_ARM64_LSE_ATOMICS`). ARMv8.1 feature, this is an
  ARMv8.0 part. The kernel compiles both paths and picks at run time via
  alternatives, so enabling it costs a few kilobytes and does nothing here.
* **Disabling `hardened_usercopy` and friends kernel-wide.** Plausible on
  paper, and the same "only runs its own software" argument applies, but these
  guards sit on syscall paths rather than in emulator inner loops, and nothing
  here has been measured. Do the measurement before the change.
* **Compiler flags alone.** clang instead of gcc, `-mcpu=cortex-a53`, LTO
  without a profile, `-mno-outline-atomics`, `-fomit-frame-pointer`: measured
  on the PS2 emulators and Dolphin (TODO.md, "Heavy emulators" and "PS2:
  ARMSX2 with a profile and ThinLTO"), none gained more than about 2%, where
  runs of one build spread by 1-2%. On the in-order A53 these emulators wait
  on memory and on JIT code, which the compiler does not reach. A profile is
  another matter and is shipped (above). Everything is still built with the
  Debian toolchain in the chroot; swapping the toolchain for the whole image
  on the assumption that newer is faster is not an experiment.
* **The START menu's shell commands.** ES's main menu runs 14 of them as it
  opens (`systemctl status bluetooth` alone 37 ms, `current_volume` 20 ms)
  and stands still for 167 ms, 10 frames, every time (2026-11-08). The
  owner: not noticeable, not worth a patch.

## If you want to measure

Start on the device, not in the build. The whole budget question is settled by
a few files:

    /sys/class/thermal/thermal_zone0/temp
    /sys/class/devfreq/ff320000.gpu/cur_freq
    /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq
    /proc/swaps
    /sys/block/zram0/mm_stat

A run that starts fast and settles slower is thermal. One that stutters with
`pswpin` climbing is memory. They want opposite fixes, and the sysfs files say
which one you have in about a minute.
