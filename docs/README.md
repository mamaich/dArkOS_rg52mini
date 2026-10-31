# RG52 Mini port notes

Working notes for this fork of [bmdhacks/dArkOS_rg52mini](https://github.com/bmdhacks/dArkOS_rg52mini),
which is itself a fork of [christianhaitian/dArkOS](https://github.com/christianhaitian/dArkOS).

They exist so the next person — or the next AI session — starts from what is
already known instead of rediscovering it. Where something was measured on
hardware it says so; where it is inference it says that too.

| | |
|---|---|
| [BUILDING.md](BUILDING.md) | environment, the WSL story, how to run a build |
| [HARDWARE.md](HARDWARE.md) | the kernel patches and what they fixed |
| [KNOWN-ISSUES.md](KNOWN-ISSUES.md) | what is missing from the image and why |
| [OTA.md](OTA.md) | updates over the air from 1.1 on, and how a release is made |
| [PERFORMANCE.md](PERFORMANCE.md) | what actually makes emulators faster here |
| [TODO.md](TODO.md) | what is next: open questions, things to try, and what was measured on the way |

## The device

    SoC       Rockchip RK3562, 4x Cortex-A53 @ 2.0 GHz, ARMv8.0-A
    GPU       Mali-G52 (Bifrost), vendor blob g29p1
    RAM       2 GB
    Panel     DSI 720x1280 portrait, console rotated (fbcon=rotate:1)
    Wi-Fi/BT  AIC8800D80, one combo part over SDIO (mmc2)
    PMIC      RK817 — regulators, codec, charger, fuel gauge
    Type-C    HUSB311 (Hynetek) on i2c@ffa10000, address 0x4e
    eMMC      3.7 GB: uboot, trust, emuelec, swap(256M), STORAGE, eeroms
    Boot      from SD: GPT, p3 FAT32 with extlinux, Image, DTB, uInitrd

The chip is marked D40 but answers as D80 over SDIO (vid `0xC8A1`,
did `0x0082`), and only the D80 code path works.

## Repositories

This fork pins its own submodules, so a fresh clone reproduces a build without
any manual steps:

    mamaich/dArkOS_rg52mini
      kernel_rk3562      -> mamaich/kernel_rk3562        hardware patches
      rk3562_core_builds -> mamaich/rk3566_core_builds   Cortex-A53 build flags, emulator patches
      bootloader         -> bmdhacks/aislpc-bootloader-tool

Clone with `--recursive`. All three are https; an earlier `git@github.com:`
in `.gitmodules` meant submodules only resolved for people with an SSH key.

## State of the port

The current release is 1.4 (build 10312026, 2026-10-31); from 1.1 on, a card
updates over the air (OTA.md). The first release of this fork was build
09162026, published 2026-09-19.
What was checked on hardware in those first weeks:

| | |
|---|---|
| Wi-Fi | works |
| Bluetooth | works — `hci0 Type: Primary Bus: SDIO`, scanning finds devices |
| USB host | works — a USB mouse enumerates, HID binds, input node appears |
| Speaker | works, and `Playback Path` is `SPK` out of the box |
| Kodi | builds and runs, 21.3-Omega with 67 addons |
| Swap | zram 1.5 GB at priority 100, eMMC partition at 10, both active |
| Kernel modules | 6.3 MB, stripped of debug info (was 100 MB) |
| Bootloader | ours, with power-off, splash, charge animation and a console |
| Bluetooth gamepads | work — the HID gate had to be opened, see HARDWARE.md |
| Kernel | `Image` 32.9 MB; no OP-TEE, no dead GPU drivers, no unread debugging |

Bluetooth and USB host are the two things the upstream port cannot do. Both
come from the device tree and driver changes described in
[HARDWARE.md](HARDWARE.md).

New in 1.3, described in [KNOWN-ISSUES.md](KNOWN-ISSUES.md) and [TODO.md](TODO.md):

| | |
|---|---|
| Nintendo 3DS | Azahar in RetroArch, with GammaOS's azahar-nano changes and a profile (PGO); Super Mario 3D Land at 60 fps |
| PS2 | ARMSX2 in place of bmdhacks' PCSX2 fork, with a profile and ThinLTO; Jak and Daxter draws right |
| Vulkan | Arm's driver or malisx2, chosen per program (`/etc/darkos/vulkan.conf`) or per system in ES; libmali is the default |
| Sleep | deep sleep instead of s2idle (~42 mA against ~185), power-off after two hours asleep on battery |
| GPU driver | Arm's g29p1-5 in place of g29p1-4, pinned by commit and sha256 |
| Ports | a fixed gl4es in `/usr/lib/gl4es` for PortMaster ports that bring none, or whose own one is renamed away |
| Keys | Home no longer toggles Bluetooth; DraStic's menu on Home+X; Azahar swaps the screens on Back |
| N64 | mupen64plus_next draws on a thread of its own (GLideN64 threaded) |

New in 1.3.1 (build 10272026, 2026-10-28):

| | |
|---|---|
| Nintendo DS | DSperate starts games again on the dual-boot card (it took the empty cheats folder there for a cheat database); its screens side by side by default |
| GameCube | Dolphin runs on malisx2 too: occlusion queries, and lines no longer fault the GPU |
| PS2 | ARMSX2's audio buffer is 100 ms; PCSX2.ini had it under old keys ARMSX2 does not read, so it was 50 ms and a slow frame emptied it |
| Ports | GTA San Andreas: the trigger axes are hidden from the port, so the camera no longer turns by itself |
| EmulationStation | QUIT EMULATIONSTATION opens BaRT instead of a black screen; on the dual-boot card a system is listed once in EMULATOR SETTINGS |
| malisx2 | built from our fork; less CPU work a draw, AFBC textures and depth, the tiler's hierarchy levels; God of War about 8% faster than in 1.3 |
| Build | SDL2 and ONScripter built with optimisation (they were -O0) |

New in 1.4 (build 10312026, 2026-10-31):

| | |
|---|---|
| GameCube | Dolphin 2609a in place of February 2023's; savestates of the old one do not load |
| Nintendo 3DS | Azahar 2126.2 and upstream master's fixes after it (patches 021-026); the disk shader cache loads on a thread behind RetroArch's progress bar (027); the texture upload ring is 128 MiB, not 512, about 350 MB less RAM (028); the colour targets are AFBC on libmali (029); on malisx2, Luigi's Mansion 2 gets past its shader preload (Mesa's register allocation, malisx2 da1e3a2) |
| Dreamcast | flycast 2.7 with master's fixes; Atomiswave keeps its NVRAM |
| PS1 | standalone DuckStation is no longer in the image: its author did not agree to the way it was passed on; PS1 games run in RetroArch, as ES starts them by default; an update to 1.4 takes it off and keeps memory cards and savestates ([KNOWN-ISSUES](KNOWN-ISSUES.md#standalone-duckstation-not-in-the-image-since-14)) |
| PS2 | malisx2 by default, 10-12% faster than libmali; malisx2 d398141 (da1e3a2 in the image builds the same code for ARMSX2): one GPU job a draw (IDVS), FP16 fragment arithmetic for ARMSX2 (God of War 5-8% faster); EE Cycle Rate down to 27% in ARMSX2's menu, 50% by default |
| Other emulators | ScummVM 2026.4.0 (MIDI through FluidSynth, no dialog about it) and openMSX 19.1 start again; XRoar 1.12.1 starts too, but its screen stays on the power-on pattern ([KNOWN-ISSUES](KNOWN-ISSUES.md#xroar-and-openmsx-did-not-start--fixed-in-14)) |
| Doom | LZDoom and GZDoom: no turning by itself, the RG52 pad's layout |
| HDMI | follows the cable on hotplug, debounced, not only at boot; sound over HDMI (the RK628's sound card had no pins, kernel eba0d6e); a game started from Ports counts as a game; RetroArch's picture is no longer on its side on a TV (patch 0025); doster00's pull requests #3-#5 |
| Scraper | ScreenScraper works again: with no developer login of the user's or the build's, ES fetches one from a friendly project's server (es-0003; [KNOWN-ISSUES](KNOWN-ISSUES.md#scraper-screenscrapers-developer-login-since-14)) |
| USB | ROMs from a PC over MTP: the console shows up in Windows as a media device "RG52 Mini" with a ROMs folder, about 21 MB/s; Options > Disable USB File Transfer turns it off ([KNOWN-ISSUES](KNOWN-ISSUES.md#usb-roms-from-a-pc-over-mtp-since-14)) |
| Scheduling | ES and ark may use realtime priority (LimitRTPRIO=99), which libmali asks for the thread that submits GPU jobs of a high-priority queue |
| RG43 Pro | `make rg43v` (doster00's pull request) |

In master for 1.5, not released yet (VERSION says 1.5; the image is built
only when the owner says so):

| | |
|---|---|
| Build | the plutovg and plutosvg sources build_pcsx2sa.sh leaves in /home/ark when it builds ARMSX2 afresh are removed; 1.4's image has them (3.3 MB), and the update to 1.5 takes them off |

Built but never launched: freej2me-plus. It had been silently missing from
earlier images and now exists — whether it runs is another question.
Yabasanshiro was in the same state until 2026-10-24, when the one game tried
crashed within seconds, in a dynarec template for an instruction as common as
MOV.B; patches 12 and 13 of its recipe make it run (KNOWN-ISSUES.md).

Not verified: whether the speaker click on playback start and stop is gone.
The GPIO behaves correctly in silence, which is necessary and not sufficient;
only an ear settles it.
