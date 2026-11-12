# For AI agents (and people) picking this port up

This is the entry page for an assistant asked to work on dArkOS for the AISLPC
RG52 Mini (Rockchip RK3562). It says where the knowledge is, how the tree is
laid out, and which rules the work here has followed so far. The knowledge
itself is in `docs/`; this page does not repeat it.

Claude Code reads `CLAUDE.md`, which this repo's `.gitignore` leaves to each
user: put the one line `@AGENTS.md` into a local `CLAUDE.md` to load this page.

## Read before changing anything

| When you are about to | Read |
|---|---|
| start at all | [docs/README.md](docs/README.md): the device, the repositories, what works, what each release brought, what is in master for the next one |
| build | [docs/BUILDING.md](docs/BUILDING.md): the build, WSL, the caches, building one component without the whole image |
| propose an optimisation | [docs/PERFORMANCE.md](docs/PERFORMANCE.md), "Not worth doing" first: the negative results cost more than the positive ones |
| diagnose a problem | [docs/KNOWN-ISSUES.md](docs/KNOWN-ISSUES.md): one section per problem with its cause, its fix and how it was checked; search it before investigating |
| touch the kernel, the loader, sound, USB, the display | [docs/HARDWARE.md](docs/HARDWARE.md) |
| change what an update has to carry, or make a release | [docs/OTA.md](docs/OTA.md) and `ota/hooks/README` |
| look for something to do | [docs/TODO.md](docs/TODO.md): open questions, what was tried, measurements per release |

The docs say where something was measured on the device and where it is
inference. Keep that distinction in what you add.

## The tree

    Makefile, build_rg52mini.sh        the build: `make rg52mini` (BUILD_KODI=y for Kodi)
    bootstrap_rootfs-rk3562.sh         Debian trixie chroot (arm64 and armhf under qemu-user)
    build_<component>.sh               one recipe per component; its pin (commit or tag) is in it
    finishing_touches-rk3562.sh        what the image gets on top: units, udev rules, scripts, settings
    scripts/rk3562/                    our scripts and units for the image (OTA client, sleep, tools)
    scripts/, global/, device/         shared with other devices (ours there: sleep.rk3562, pause.sh)
    Emulationstation/patches-rk3562/   our ES patches, es-NNNN-<what>.patch
    retroarch-patches/, sdl2-patch-*   our patches to RetroArch and SDL2
    dArkOS_Tools/rk3562/               Options -> Tools entries
    malisx2/, pcsx2/, dolphin/, ...    per-component files: settings, launchers, profiles
    rk3562_core_builds/                submodule: RetroArch cores, one recipe each, patches/<core>-patch-NNN-*.patch
    kernel_rk3562/                     submodule: the kernel with the hardware patches
    bootloader/                        submodule: the loader packing tool
    ota/make-ota.py                    makes an update package from two images
    ota/hooks/                         what an update runs after its files are in place
    ota/rg52mini.json                  the feed devices read; an entry here is a published update
    tools/upstream/                    check.py and components.json: what is new upstream of each pin
    VERSION                            the number of the next release

Files with `rk3562` or `rg52mini` in their names are this device's. The others
are upstream's ([christianhaitian/dArkOS](https://github.com/christianhaitian/dArkOS)
through [bmdhacks/dArkOS_rg52mini](https://github.com/bmdhacks/dArkOS_rg52mini))
and serve other devices too: change them only when the RG52 Mini needs it, so
that upstream's changes keep merging.

Related repositories, each with its own history and notes:
[mamaich/kernel_rk3562](https://github.com/mamaich/kernel_rk3562),
[mamaich/rk3566_core_builds](https://github.com/mamaich/rk3566_core_builds),
[mamaich/u-boot-rk3562-rg52mini](https://github.com/mamaich/u-boot-rk3562-rg52mini)
(the loader the image ships),
[mamaich/malisx2-rg52mini](https://github.com/mamaich/malisx2-rg52mini) (the
open Vulkan driver; the image builds it at a pinned commit, its development is
there).

## How the work is done here

**Pins.** Every outside component is built at a commit or tag written in its
recipe; `tools/upstream/components.json` says where. A pin stands for "this
version was tried on the device". Moving one means: build the component (the
chroot from the cache, BUILDING.md), run it on the console beside the version
it replaces, on the same game, and write the numbers into TODO.md. Do not move
a pin because upstream is newer.

**A component is cached by its commit and by a hash of its recipe and
patches** (`Arkbuild_package_cache/`). Adding or changing a patch rebuilds that
component at the next build; nothing else does. A full build is about two
hours with warm caches, most of a day without.

**Measure on the device.** A change that claims speed comes with frames per
second before and after, several rounds, alternating the two versions; the
method is in PERFORMANCE.md, "If you want to measure". Start an emulator the
way EmulationStation starts it (its launcher, `perfmax`, the same
environment), or the number says nothing about what a user gets. A result of
"no difference" or "slower" is written down as well: several ideas that look
obvious (more compiler flags, power-budget weights, OpenGL ES for the 3DS
core) were tried and are recorded as not worth doing.

**Write the cause, not only the fix.** A problem gets a section in
KNOWN-ISSUES.md: what was seen, what causes it, what fixes it, how that was
checked, and the release ("— fixed in 1.5" in the title once released, "for
1.10" until then). A change a user can notice gets a line under "In master for
<next>" in docs/README.md. The docs change in the same commit as the code.

**Patches carry their reason.** Each patch file starts with what it changes
and why; a recipe applies its patches in number order. Patches fit to go upstream are listed in TODO.md,
"Worth sending upstream".

**Updates.** A released card is updated by a package made from two images
(`ota/make-ota.py`), so most changes need nothing extra. What a file copy
cannot do (a setting inside a file the user may have edited, an entry in
`es_systems.cfg`, a service to enable) needs a hook `ota/hooks/<version>-<name>.sh`,
safe to run twice. A package runs the hooks of every release after the one it
updates from. An update never writes the loader or foreign partitions of a
card shared with GammaOS (OTA.md, "Dual-boot card").

**Commits.** In English. The subject says what changes for the user or the
build; the body says the cause and how it was checked on the device.

## Things settled

Do not reopen these without new evidence; each has its section in the docs.

- Standalone DuckStation is not in the image and does not come back: its
  author's licence terms (KNOWN-ISSUES.md, "Standalone DuckStation").
- Release notes describe changes in general words and do not single out games
  that still run too slowly to play; the measurements stay in the docs.
- No credentials in the tree: the scraper's developer login comes from a
  git-ignored `exports.sh` at build time or from the user's own settings
  (KNOWN-ISSUES.md, "Scraper: ScreenScraper's developer login").

## Working beside the owner

This is one person's fork with regular releases; `master` is what the next
release is built from. Work in your own fork or branch and send a pull
request against `mamaich/dArkOS_rg52mini` (for a core's recipe or patch,
against `mamaich/rk3566_core_builds`; the pointer in this repo then moves in a
second request). In the request say what was run on real hardware, on which
release, and what was measured; a change tried only in a build is welcome but
say so. Keep one topic per request: the owner tests each on a console before
it goes into a release. Releases, tags and the OTA feed are the owner's to
make.

On a console you test with: back up saves and settings you are going to
change, and do not write to the eMMC or to the loader partition unless that
is the task (HARDWARE.md, "The bootloader" and "The memory clock", say what is
kept where and what it takes to bring a console back).
