# What is next

A working list, not a roadmap. Ordered by what a build would answer soonest.

## Shipped in v09162026

All of it is on the device and released, so it is history rather than a list to
work through: the boot logo from both halves, `Playback Path = SPK` by default,
zram on lzo-rle with the eMMC partition as a second tier, stripped modules,
Cortex-A53 flags for everything that rebuilt, Kodi, Yabasanshiro, freej2me-plus,
our own U-Boot, and the component audit that runs at the end of every build.

Nothing is queued behind it. The kernel changes that landed after the build
finished — the RK628 bridge fix, the hang detectors, the bootloader log — were
rebuilt and written into the released image by hand rather than left waiting,
so the release and the tree say the same thing.

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
* Seven PortMaster compatibility libraries whose URLs 404.

Both are described in KNOWN-ISSUES.md with what was already ruled out.

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

The other two — the absolute toolchain path and the 32-bit chroot cloning
upstream's core builds instead of the local fork — only bite in this fork's
layout.
