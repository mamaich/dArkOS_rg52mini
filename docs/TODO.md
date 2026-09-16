# What is next

A working list, not a roadmap. Ordered by what a build would answer soonest.

## Waiting on the next build

Everything here is committed and unbuilt — the 2026-09-15 image predates it.
`make rg52mini` is enough; `BUILD_KODI=y` is deliberately left off until the
other changes have been on the device for a while.

* **Boot logo.** The kernel side, the command line and the artwork are all in
  place. The screen should show the vendor picture instead of staying black.
  See HARDWARE.md if it does not — the two non-obvious requirements are
  `loglevel` above 4 and no `console=tty1`.
* **Speaker by default.** `audio/asound.state.rk3562` now stores
  `Playback Path = SPK`. Before this, a fresh image played to headphones that
  were not plugged in and the device seemed mute.
* **zram on lzo-rle by choice**, with `lz4` compiled in as an alternative.
* **eMMC swap.** Second tier at priority 10 on the 256 MB `swap` partition.
  Worth watching the first boot: the service waits for the partition by GPT
  label and refuses to format anything it does not already recognise as swap.
* **Stripped modules**, 100 MB → 6.3 MB.
* **Cortex-A53 flags** in the core builds. The thing to check is whether the
  Amiga emulator runs at all now — see KNOWN-ISSUES.
* **The component audit** at the end of the build. Read what it prints.

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
* Yabasanshiro's upstream repository is gone and no mirror has the pinned tag.
* Seven PortMaster compatibility libraries whose URLs 404.
* Kodi's two patch conflicts — the workaround is committed but unbuilt.

All four are described in KNOWN-ISSUES.md with what was already ruled out.

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
