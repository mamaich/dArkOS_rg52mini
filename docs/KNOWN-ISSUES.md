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

Four fail the same way: a patch in `rk3562_core_builds/patches/` no longer
applies to its upstream, `builds-alt.sh` stops, and the copy that follows
finds nothing.

| | |
|---|---|
| ECWolf | `ecwolf-patch-002-add-exit-menu.patch` |
| Hypseus Singe | `hypseussinge-patch-0001-buildfix.patch` |
| GameTank | `gametank-patch-001-disable-joystick.patch` |
| Yabasanshiro | upstream repository is gone, see below |

Fixing the first three means working out what changed upstream and rewriting
the hunks — one job each, not a batch. They are all secondary emulators.

**Yabasanshiro is different.** `scripts/yabasanshirosa.sh` clones
`https://github.com/devmiyax/yabause` at tag `pi4-1-9-0`, and the repository
no longer exists — git asks for a username and the build reports it as a
network problem. There is no drop-in replacement; none of the surviving
mirrors carries that tag:

    devmiyax/yabause                            gone
    sydarn/yabasanshiro                         no tags at all
    Mechafatnick/YabaSanshiroPi                 no such tag
    pirrypirrypirry/yabasanshiro-pirry-release  four tags, not this one
    gfhhhg/lr-yabasanshiro                      no such tag
    libretro/yabause                            a different project

Picking another fork means picking an arbitrary revision and checking the
recipe still applies. The libretro Saturn core is built and works, so only the
standalone is missing.

Watch out for the cache when changing any of this: `build_yabasanshirosa.sh`
takes its cache key by curling the **upstream** christianhaitian script for its
`TAG=`, not our fork's. Change the tag here and the key does not move, so a
stale tarball is restored over the new build. The same trap applies to any
component whose TAG we edit.

## Kodi

Kodi did not build in the 2026-09-15 image:

    DRMAtomic.cpp  Hunk #1 FAILED
    There was an issue applying kodi-patch-001-rk3562-rga-rotation.patch
    tar: Arkbuild/opt/kodi: Cannot stat: No such file or directory

Kodi itself is pinned at 21.3-Omega, so that is not the problem.
`kodi-install` is cloned at master, and it has grown two patches that land on
the same files as ours: `0017-kodi-patch-mali-egl-display` is literally our own
EGL fix, which upstream adopted, and `0016-miniloong-internal-only-rotate270`
rotates another device the other way through the same DRM paths our 90-degree
RGA rotation uses.

`build_kodi.sh` now removes both before staging ours. Untested — the next
build with `BUILD_KODI=y` will say.

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

## PortMaster compatibility libraries

Seven of the twenty-one URLs in `fetch_compat_libs.sh` are dead, all of them
pinning a bullseye version on `security.debian.org`, which drops superseded
packages as a release ages: libavcodec58, libavformat58, libavutil56,
libswresample3, libswscale5, libvpx6, libaom0.

Those versions are gone from `archive.debian.org` and from the snapshot API as
well, so there is no URL to switch to — only different versions, which is a
decision rather than a fix. The build warns and carries on now; it used to
`exit 1` and take the whole run down with it, nine hours in.

PortMaster ports that need those libraries will not start. Everything else is
unaffected: the custom rkmpp ffmpeg provides its own libav* and is what the
system uses.

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
