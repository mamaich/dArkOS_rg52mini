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
  2 MiB SHM window. See HARDWARE.
* **RetroArch gets its cores even when GitHub says 429.** 269 cores
  downloaded, 8 of them only after a retry; plain `wget -t` counts network
  failures and treats an HTTP error as a final answer, so those 8 used to go
  missing silently.

## Revision A black screen: find what cycles the display, then drop panel-kick

What breaks the panel is known (KNOWN-ISSUES): the display switched off and on
again faster than the DSI host and its PHY runtime-suspend. What is not known is
what does that to revision A at boot — revision B never goes through such a
cycle. `panel-kick` repairs the result; the cause should be found and removed.

1. Ask a revision A owner for `dmesg` over ssh. Look for a
   `vop2_crtc_atomic_disable` followed shortly by `vop2_crtc_atomic_enable`
   after the first enable, and for what logs just before it.
2. The proper kernel fix is in `dw-mipi-dsi-rockchip`: an enable that follows a
   disable should bring the PHY and the host up from scratch whether or not
   runtime PM got as far as suspending them. With that, neither revision would
   care how fast a cycle comes, and `panel-kick` can go.
3. Does v09162026 also go black on revision A? Still unasked, and it says
   whether something between the releases introduced the cycle.

## HDMI: find the real connector name, then make detection follow the cable

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

* **Is the speaker click gone?** `spk-mute-delay-ms = <100>` is in the device
  tree and the GPIO reads `out lo` in silence, which is necessary but does not
  prove the click went away. Needs an ear.
* **Does a USB mouse do anything?** It enumerates and binds `hid-generic`, and
  an input node appears, but no cursor is visible. EmulationStation may simply
  not use one — that would be expected, not a fault.
* **`OF: graph: no port node found`** in the husb311 connector. Role switching
  works anyway. `backup/dtb/live.dtb` from the vendor's EmuELEC has the
  complete port/endpoint graph if this is ever worth tightening.

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

## Repairs, each its own small job

* Three bit-rotted patches: ECWolf, Hypseus Singe, GameTank.

Described in KNOWN-ISSUES.md with what was already ruled out.

The seven PortMaster compatibility libraries that used to 404 are fixed:
upstream found them on snapshot.debian.org and this fork took the change. All
twenty-one URLs answer now.

Done since this list was written: Yabasanshiro, whose upstream repository is
gone, builds from a pinned commit of a surviving fork; freej2me-plus needed a
java level a current JDK still accepts and a jar name that had been renamed
under it; and Kodi's two patch conflicts are resolved and built.

## Worth sending upstream

Five build-system defects were fixed in this fork. Three of them have nothing
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

## Delete two cached failures before the next build

`make rg52mini` ends with an audit, and on 2026-10-01 it reported:

    EMPTY       /opt/ecwolf
    NO BINARY   /opt/gametank  (files present, no ELF among them)
    NO BINARY   /opt/hypseus-singe  (files present, no ELF among them)
    with binaries: 33   empty: 1   no executable: 2

Two of those are self-perpetuating. `Arkbuild_package_cache/rk3562/` holds
`ecwolfsa.tar.gz` at 130 bytes and `gametank.tar.gz` at 128 bytes — empty
tarballs written when the build failed, under keys that still match. Every
later build restores the failure instead of retrying it, which is why they are
missing from the 2026-09-29 image as well. Delete those two `.tar.gz` files
with their `.commit` partners and the next build will try again.

What it would then have to get past: `ecwolf-patch-002-add-exit-menu.patch`,
`gametank-patch-001-disable-joystick.patch` and
`hypseussinge-patch-0001-buildfix.patch` all failed to apply, and `libfuse2`
and `libpcap0.8` do not exist in trixie under those names.
