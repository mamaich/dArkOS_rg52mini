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

Two more of our repositories feed the build without being submodules: malisx2
is cloned from mamaich/malisx2-rg52mini at the commit in `build_malisx2.sh`
(the driver is developed there), and U-Boot is a committed binary,
`BSP/uboot-rg52mini.img`, built from mamaich/u-boot-rk3562-rg52mini
(HARDWARE.md, "The bootloader"). Every other pin is in a recipe;
`tools/upstream/components.json` says in which file (BUILDING.md).
[AGENTS.md](../AGENTS.md) at the top of the tree is the entry page for an AI
assistant, or a person, picking the work up: the layout and the rules it has
followed.

## State of the port

The current release is 1.9 (build 11122026, 2026-11-12); from 1.1 on, a card
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
| Other emulators | ScummVM 2026.4.0 (MIDI through FluidSynth, no dialog about it) and openMSX 19.1 start again; XRoar 1.12.1 starts too, but its screen stays on the power-on pattern ([KNOWN-ISSUES](KNOWN-ISSUES.md#xroar-and-openmsx-did-not-start--openmsx-fixed-in-14-xroar-in-15)) |
| Doom | LZDoom and GZDoom: no turning by itself, the RG52 pad's layout |
| HDMI | follows the cable on hotplug, debounced, not only at boot; sound over HDMI (the RK628's sound card had no pins, kernel eba0d6e); a game started from Ports counts as a game; RetroArch's picture is no longer on its side on a TV (patch 0025); doster00's pull requests #3-#5 |
| Scraper | ScreenScraper works again: with no developer login of the user's or the build's, ES fetches one from a friendly project's server (es-0003; [KNOWN-ISSUES](KNOWN-ISSUES.md#scraper-screenscrapers-developer-login-since-14)) |
| USB | ROMs from a PC over MTP: the console shows up in Windows as a media device "RG52 Mini" with a ROMs folder, about 21 MB/s; Options > Disable USB File Transfer turns it off ([KNOWN-ISSUES](KNOWN-ISSUES.md#usb-roms-from-a-pc-over-mtp-since-14)) |
| Scheduling | ES and ark may use realtime priority (LimitRTPRIO=99), which libmali asks for the thread that submits GPU jobs of a high-priority queue |
| RG43 Pro | `make rg43v` (doster00's pull request) |

New in 1.5 (build 11012026, 2026-11-02):

| | |
|---|---|
| Build | the plutovg and plutosvg sources build_pcsx2sa.sh leaves in /home/ark when it builds ARMSX2 afresh are removed; 1.4's image has them (3.3 MB), and the update to 1.5 takes their files off: the two folders stay with their `.git` in them, about 600 KB, since make-ota skips `.git` |
| malisx2 | 4c91f3c: upstream's 0.3.1 merged (dynamic rendering, timeline semaphores, events and real occlusion queries advertised; Mesa e07f62e); Castlevania 3-5% faster, a first start's shader compile about 4% longer (Luigi's Mansion 2: 100 s against 96); a profile trained on this code; 428b5b2: the DuckStation core on RetroArch's Vulkan driver no longer crashes on it |
| GameCube | Dolphin master c4856ead (2026-10-31) in place of 2609a: the ND Demo within the spread, 2609a's savestates load (same version) |
| Nintendo DS | DSperate f608b4c: the ARM9's IPC FIFO at send time, the lean rasterizer's attributes as the DS has them, a stylus speed option |
| GBA | armGBA, a core written for ARM, as a choice in EMULATOR SETTINGS beside mGBA, which stays the default; not offered where an update kept a hand-edited es_systems.cfg (TODO.md, "For 1.6") |
| SDL2 | 2.32.10 with release-2.32.x's 21ac66c8b: SDL_AtomicSet with a full barrier on ARM |
| Ports | gl4es bc4314d: wide lines drawn as quads where the driver has only 1 px |
| EmulationStation | UI Settings > START ON SYSTEM > LAST PLAYED SYSTEM, the default in place of NONE, after an update too (NONE can still be chosen): ES opens on the system of the game played last, with that game selected (es-0005; [KNOWN-ISSUES](KNOWN-ISSUES.md#es-start-on-the-system-last-played--since-15)) |
| USB pads | pads in Xbox 360 mode work: the kernel has xpad, with the init read some 360 clones wait for (kernel 1c9d338; [KNOWN-ISSUES](KNOWN-ISSUES.md#usb-pads-in-xbox-360-mode-did-nothing--fixed-in-15)); ES knows a pad it has a config for again (es-0006); in RetroArch the USB pads ES has a mapping for are players 1, 2…, then the console's controls, each pad by a profile made from ES's mapping, the hotkeys on player 1's pad (`ra-extpads.py`; [KNOWN-ISSUES](KNOWN-ISSUES.md#usb-pads-es-ignored-them-retroarch-put-them-after-the-console--fixed-in-15)) |
| Select | on ES's screen Select puts the screen out and lights it again reliably: its release no longer restarts the screensaver, the black screensaver keeps its brightness (es-0007), and Select's release has a 100 ms debounce (kernel; [KNOWN-ISSUES](KNOWN-ISSUES.md#select-on-ess-screen-the-screen-went-out-again-presses-seemed-lost--fixed-in-15)) |
| Headphones | with headphones in, the speaker is silent, during a game too: the playback path follows the jack (`headphone-path.sh`, run by a udev rule), and the codec turns the speaker's amplifier off when the path changes mid-stream (kernel d188e7c); with HDMI in, headphones win ([KNOWN-ISSUES](KNOWN-ISSUES.md#headphones-the-speaker-played-too--fixed-in-15)) |
| EmulationStation | UI Settings > Show Clock shows the clock (at the bottom right, it was below the screen); the battery's percent at the top right is on the screen; nes-box shows a game's logo whole, in the bar at the bottom (es-0004, `1.5-nesbox-marquee.sh`; [KNOWN-ISSUES](KNOWN-ISSUES.md#es-no-clock-the-batterys-percent-off-the-screen-a-games-logo-in-the-corner--fixed-in-15)) |
| PortMaster | its window fills the screen, no black bars on the sides: `portmaster-e2big.sh` gives it the screen's size ([KNOWN-ISSUES](KNOWN-ISSUES.md#portmasters-window-had-black-bars-on-the-sides--fixed-in-15)) |
| RetroArch | a second choice in EMULATOR SETTINGS, `retroarch-next`: RetroArch from master (fab8d5f, 64-bit), our patches ported to it, its own config; v1.22.2 stays the default ([KNOWN-ISSUES](KNOWN-ISSUES.md#retroarch-from-master-retroarch-next-a-second-choice-since-15)) |
| XRoar | the Dragon 32 and the CoCo 1/2 get to BASIC: `-ram 2048` only for a CoCo 3; the Dragon 32 system's launcher `dragon.sh` is installed ([KNOWN-ISSUES](KNOWN-ISSUES.md#xroar-and-openmsx-did-not-start--openmsx-fixed-in-14-xroar-in-15)) |
| Sleep | not changed, but the notes advise unplugging USB devices before sleep and plugging a charger into an awake console: a USB headset on the port turns deep sleep into a loop of wakes ([KNOWN-ISSUES](KNOWN-ISSUES.md#usb-devices-on-the-port-in-sleep-it-woke-again-and-again--advice-in-15-not-fixed)) |

Checked for 1.5 on the dual-boot card: the update 1.4 -> 1.5 from `ROMs/update`
(70 files, every hook, the 1.4 snapshot kept), then the engines that changed,
each game as ES starts it, on 1.4 before the update and on 1.5 after it, the
same card: Dolphin (ND Demo 46.5 -> 47.5 fps), DSperate (59.5 -> 59.1), DraStic,
PPSSPP, flycast (Crazy Taxi, Atomiswave), PS1 in RetroArch32 and the DuckStation
core on Vulkan (Arm's driver, and malisx2, which 1.4 crashed on), Azahar, ARMSX2
on malisx2 (God of War 43.7 -> 44.0), Mina the Hollower, Mega Drive in RetroArch
and retroarch-next, GBA in mGBA and armGBA (6% CPU against 8%), N64; XRoar's
Dragon 32 and CoCo at BASIC's OK. No crash, the sound playing, the speed the
same.

New in 1.6 (build 11032026, 2026-11-04):

| | |
|---|---|
| Emulators | ARMSX2 master f6df1e69 with a profile retrained for it: Jak and Daxter +5%, Guitar Hero +6%, Burnout 3 +2%, God of War within the spread; DSperate v3.2.0 (PGO profiles refreshed, 3D fixes); Dolphin master e8c4246; the SDL3 shim with elderica's audio fix (PS2's sound when the device opens in another format) |
| OTA | `1.6-es-armgba.sh`: armGBA offered for GBA in an es_systems.cfg the update to 1.5 kept |
| Apple II | linapple master c984d03 (Slayer366's work: a startup chooser, an on-screen keyboard, SDL2 fullscreen fixes); our joystick-log patch dropped with upstream's 1857f40; the packages its build installs (imagemagick and 35 more) purged after it (2e2d763) |
| PS2 | AutoTune off by default, as ARMSX2 master has it; EE Cycle Skip up to 4, "Extreme Underclock" (pcsx2-0008), by hand: the game's own speed to full at fewer frames (God of War 28 -> 50 of 50 at the same 21 frames a second) |
| PS2 | ARMSX2's EE Cycle Rate down to 21% (pcsx2-0006), and Full EE Speed in Movies, on by default (pcsx2-0007): movies that stop at a low EE rate play; Guitar Hero at 21% about 59 vps on average ([KNOWN-ISSUES](KNOWN-ISSUES.md#guitar-hero-ps2-its-movie-stopped-its-speed-swung--in-16)) |
| Triggers | the dead zone at rest doubled to 1600 of 32767 (kernel 4e1991c): L2 no longer reads as pressed by itself in ARMSX2 ([KNOWN-ISSUES](KNOWN-ISSUES.md#l2-pressed-by-itself-in-armsx2--fixed-in-16)) |

Checked for 1.6 on the console: the update 1.5 -> 1.6 from `/roms/update` (46
files, every hook, the 1.5 snapshot kept; the files match the image's), and a
rollback to 1.5 from that snapshot before it; then each game as ES starts it:
ARMSX2 (God of War), Dolphin (ND Demo 50.3 fps), DSperate (Diddy Kong 60), GBA,
SNES, PPSSPP, linapple. No crash, the sound playing. ARMSX2's speed was
measured before the build, from the owner's savestates in four games
(TODO.md, "Upstream updates seen 2026-11-02"). The packages from 1.4, 1.3.1,
1.3, 1.2 and 1.1 were made the same way from the published images and not
installed.

New in 1.7 (build 11062026, 2026-11-06):

| | |
|---|---|
| malisx2 | 083892f: upstream's 0.3.2 (a tiler heap ring that grows, linear descriptor pools) and four v7 commits of pjperez' fork (image-only barriers narrowed, by-region barriers inside a pass without a barrier draw, BC1-7 sampled, 37 extensions in place of 28); in Bifrost's shader compiler (Mesa a63666c) spill slots aligned, if conditions known from uniforms folded, loop flags from one counter, cheap values recomputed where used; a profile trained on this code. On the console against 1.6's: God of War and Jak and Daxter from the owner's saves within the spread, Castlevania 3-5% faster, Luigi's Mansion 2's intro about twice as fast, at libmali's rate (the malisx2 session); a new shader cache ID, so each game compiles its pipelines once more ([KNOWN-ISSUES](KNOWN-ISSUES.md#vulkan-arms-driver-or-malisx2-chosen-per-system-13), "malisx2 0.3.2"). Then 083892f, which 1.7's image was rebuilt with: a spilled shader compiled again with more registers to spare, Luigi's Mansion 2 5% faster, the other games the same (TODO.md, "1.7 rebuilt with malisx2 083892f") |
| mupen64plus (standalone) | upstream's master of 2026-11-06: bounds checks in the Rice and Glide64mk2 plugins, Rice's texture cache no longer grows without bound, Game Boy cartridges mirrored and the MBC3 clock started for the Transfer Pak; Zelda Ocarina of Time as fast as before on Rice, Glide64mk2 and GlideN64 |
| 351Files | its file list shows again: since upstream's a6a3f49 its UI sizes were 0 on our devices and the screen stayed blank, 1.6's too (doster00's pull request #6) |
| J2ME | the freej2me core runs on the image's OpenJDK 21, no JDK 11 download, and is offered first (pull request #8) |
| RG43H / RG43V Pro | brought up to the RG52 Mini's level: ECWolf's menu, the FN button and Bluetooth, ES's script, zram sized from the RAM, PS2 and N64 settings, the gl4es builds (pull request #7); on the RG52 Mini nothing changes |
| ARMSX2 | master 1049db37 of 2026-11-04 (75 commits after 1.6's), with a profile retrained for it. From the owner's saves against 1.6: God of War within the spread, Jak and Daxter +7%, Guitar Hero +1%, Burnout 3 the same; 1.6's savestates load (TODO.md, "For 1.7") |
| Dolphin | 584366c; ND Demo and Metroid Prime as fast as 1.6's (45.7 / 45.5 and 35.2 / 35.2) |
| DSperate | v3.3.0 |
| linapple | upstream's SDL2 build (no sdl12-compat), and apple2.sh's HDD1/HDD2 handling from upstream dArkOS; 1.6 still had the SDL 1.2 build, as the recipe's cache was keyed by linapple's commit alone |
| 351Files | Chinese, Japanese and Korean file names drawn with Noto Sans CJK instead of boxes; Latin and Cyrillic ones as before |
| Bootloader | U-Boot rg52mini-1.5 with the overclocking tables (1.8 ships rg52mini-1.6, below): the system menu on a dual-boot card, a long press that leaves the charge screen no longer lost; the OTA writes it to a card with dArkOS alone ([HARDWARE](HARDWARE.md#the-bootloader)) |

Checked for 1.7: on the owner's new dual-boot card (card v1.5) the update
1.5 -> 1.7 from `ROMs/update` (73 files, 124 deleted, every hook, the 1.5
snapshot kept; the files match the image's), U-Boot, `resource` and p5 left
alone ("dual-boot card ... not touched"), up in 16.9 s with no failed unit, and
GammaOS boots from the menu after it. On the console the first 1.7 image went
on over 1.6 by its package (38 files), then the rebuilt image over the test 1.7
by its own; each engine that changed as ES starts it: ARMSX2 (God of War),
Dolphin (ND Demo), DSperate (Castlevania DoS), Azahar, openMSX, linapple,
mupen64plus standalone (Rice, Glide64mk2, GlideN64). No crash, the sound
playing. malisx2's and ARMSX2's speed was measured before the builds against
the versions they replace (TODO.md, "For 1.7" and "1.7 rebuilt with malisx2
083892f"). The packages from 1.6, 1.4, 1.3.1, 1.3, 1.2 and 1.1 were made the
same way from the published images and not installed.

New in 1.8 (build 11082026, 2026-11-08):

- U-Boot rg52mini-1.6 in place of rg52mini-1.5: on a dual-boot card booting
  GammaOS the Type-C probe now works, so a revision A board gets its panel kick
  (HARDWARE.md, The bootloader). The OTA packages to 1.8 write it to a card
  with dArkOS alone.
- Upstreams of 2026-11-07 (docs/TODO.md, "Upstream updates for 1.8"):
  RetroArch recovers a PCM left in SETUP (HDMI sound on its first start),
  Hypseus gets per-game -fastboot and DIP banks, ES starts 1.5 s sooner (it no
  longer waits for the welcome message), DSperate ff08a3c (v3.4.0 and after:
  a menu language and Cyrillic in its menus, filters and colour correction on
  the GPU, less CPU in Diddy Kong Racing), Dolphin 4a70679, retrorun 3.2.5.
- ARMSX2 2.9.2 (ba68927 of 2026-11-06) with a profile retrained for it:
  against 1.7's, Jak and Daxter from the owner's save the same, Burnout 3
  1.7% faster; the owner's savestate loads, our PCSX2.ini reads as before
  (TODO.md, "Upstream updates for 1.8").
- malisx2 0.4.0 (2a2a2cd, Mesa 3b0e036; handed over by the malisx2 session on
  2026-11-08 with the owner's word) with a profile retrained for it:
  upstream's 0.4.0 merged whole (push descriptors, indirect draws and
  dispatches, VkEvent, synchronization2, parallel-RDP) and our indirect draws
  through IDVS on top, with 0.3.3's fixes (master from 2026-11-07: ARMSX2 at
  3x no longer loses the device in God of War, instanced draws take
  firstInstance). Against 0.3.3: God of War +0.8%, Jak and Daxter +1.2%,
  Castlevania the same. Its shader cache ID is new, so emulators on malisx2
  rebuild their pipeline caches once.
- Zeebo: ZeebxTeam's zeebx core (v0.5.1) under RetroArch, ES system "zeebo"
  reading /roms/zeebo (a game zip as the console lays it out: `mif/<id>.mif`
  and `mod/<id>/...`; a zip with every file at its root stops at "IMICRO3D
  failed creation"). Two patches of ours: frames drawn in 2D reached a black
  screen under RetroArch's GL driver, and the 2D blits are cheaper (Action
  Hero 3D at about 60% of full speed, playable). The update adds the system
  to a user's own es_systems.cfg (ota/hooks/1.8-zeebo.sh).
- retrorun's games show their colours right: the kernel now swaps red and blue
  for RGB888 as the RK3562's display controller needs (Crazy Taxi was blue
  instead of yellow).
- ES no longer runs brightnessctl 20 times a second to watch the brightness
  (`es-0008`: it reads the backlight's files in sysfs): idle in the menu the
  whole system takes 36% of a core instead of 56%, the CPU's mean clock 719
  MHz instead of 774 (PERFORMANCE.md).
- Options -> Tools -> DDR Overclock (RG52 Mini): writes the memory loader that
  runs the memory at 928 MHz instead of 798 to the internal eMMC, after a
  warning that has to be accepted, or puts the stock one back; English or
  Russian after ES's language (HARDWARE.md, "The memory clock").

Checked for 1.8: on the owner's dual-boot test card (1.7 with malisx2 0.4.0,
ARMSX2 2.9.2, the DDR tool and a kernel put on by hand) the update 1.7 -> 1.8
from `ROMs/update` (109 files written, the 3 put on by hand among them, 7
deleted, every hook, the 1.7 snapshot kept; the files match the image's), p1,
p2 and p5 left alone ("dual-boot card ... not touched"), up with no failed
unit and the memory at 928 MHz as before. A first try stopped at the space
check (19 MiB free on /boot, 43 needed: a spare kernel of ours lay there) and
changed nothing. Then each as ES starts it: ARMSX2 (God of War), Dolphin (ND
Demo), DraStic and DSperate (Diddy Kong Racing; DSperate also Castlevania
DoS), Azahar (Castlevania), PPSSPP, N64 (AeroGauge), Dreamcast (Crazy Taxi),
Saturn (Resident Evil), Zeebo (Action Hero 3D), GBA, SNES and linapple: the
picture right, the sound playing, no crash while playing. Not new in 1.8:
Azahar stopped by SIGTERM sometimes ends in dynarmic's "Unhandled SIGSEGV"
(one stop in two under 1.7's retroarch as under 1.8's, bound from the OTA
snapshot; the core is the same file), and AeroGauge's attract mode cuts
between cameras with a black wedge under both retroarch32 builds. The
packages from 1.6, 1.5, 1.4, 1.3.1, 1.3, 1.2 and 1.1 were made the same way
from the published images and not installed.

New in 1.9 (build 11122026, 2026-11-12):

- Options -> Update says what is wrong with a file in the update folder
  instead of passing over it: damaged or cut short in the download, made for
  another build, not a .tar (`darkos-ota local-problems`, OTA.md); "up to
  date" names the build. Checked on the console with a 47-byte package, a
  package from 1.5 to 1.7 and a .part file.

- ARMSX2 gets PCSX2's game patch database: `resources/patches.zip` made from
  PCSX2/pcsx2_patches b919d7f (2026-11-09; first taken at 192ee9b of
  2026-10-31, which the checks below used) as their CI makes it; before, every
  game start logged "Failed to open patches.zip" (TODO.md). Nothing is applied
  by itself: every one of its 4719 files keeps its patches in named groups, and
  ARMSX2 enables only unlabelled ones on its own. The pause menu (Select+X) ->
  Settings -> Graphics Settings has "Apply Widescreen Patches" and "Apply
  No-Interlacing Patches" for every game, Game Properties -> Patches the rest
  per game. Checked on 1.8 with the zip bound in (2026-11-09, Jak and Daxter):
  as it is, 3 patches found and none on; with widescreen on, "Widescreen 16:9"
  enabled and the picture 16:9 across the whole panel; No-Interlacing enabled
  from the game's settings; the speed the same in all three (KNOWN-ISSUES.md).

- Before a game starts, the BIOS files its emulator cannot start it without
  are listed on the screen with the folder they go to, instead of a silent
  return to ES; when RetroArch still fails at once, it starts again with its
  log on and its error lines are shown (`darkos-bios-check`, KNOWN-ISSUES.md
  "BIOS check before a game starts"). The rules come from the cores' sources,
  not their .info lists. Options -> Advanced -> Disable BIOS Check turns both
  off. Checked on 1.8 with an empty folder over /roms/bios (2026-11-09): Sega
  CD, PS2 (in Russian), Atomiswave and Lynx boxes, the second start's box with
  the core's own line, no box with the owner's BIOS, the switch both ways.

- Opera (3DO) and SwanStation (PS1) are built from upstream, pinned 595fd1d
  and 631f0fb, instead of rg503's builds of 2024. Both now start a game
  without a BIOS file: Opera on its built-in Opera Boot (its default even with
  a file; Core Options -> BIOS (rom1) picks the file), SwanStation on
  OpenBIOS. SwanStation's RK3566 rumble patch is dropped: it wrote to PWMs that
  are the fan and the backlight here, and never could. Checked on 1.8
  (2026-11-09): Opera Boot's screen with and without files; Double Dragon at
  60 fps on both SwanStation builds, the new one also with no BIOS
  (KNOWN-ISSUES.md).

- OpenCL through the standard loader (ocl-icd), for 64-bit and 32-bit
  programs: /etc/OpenCL/vendors/mali.icd points it at the Mali blobs, OpenCL
  3.0 in both, and the 32-bit loader is installed. Debian 11's FFmpeg 4
  libraries for 32-bit ports could not load at all: 22 libraries they link
  were missing, and libx264 asked for an executable stack, which glibc 2.41
  refuses to dlopen(). The build installs the missing ones and clears the flag.
  Checked on the 1.9 test build (2026-11-09): a kernel on the GPU through the
  loader in both, ES's video previews with and without the file, 300 frames of
  H.264 decoded by the 32-bit libraries (KNOWN-ISSUES.md).

- Options -> Update says when another update follows the one it installs: from
  1.9 a release brings packages from the two releases before it only, and an
  older card updates in steps (`darkos-ota check` lists what follows, `darkos-ota
  next`). A rollback merges the package database instead of putting the old
  one back: what was installed, removed or upgraded with apt after the update
  stays as it is, and what the update changed goes back to the old build's,
  files included. Checked on the console (2026-11-09): a rollback from the 1.9
  test build to 1.8 with 27 packages installed after the update, then 1.9 again
  through the menu from a test feed (OTA.md).

- malisx2 0.5.0 (86b16a8, on top of the 0.4.1 below; handed over by the
  malisx2 session on 2026-11-12 with the owner's word): upstream's 0.5.0
  (other Mali GPUs; nothing new for the G52) and a shader's uniform words in
  the whole 64-word FAU block for fragment shaders: Castlevania on the Azahar
  core 3% faster, 4.5% with the same for vertex shaders, which the RetroArch
  launcher asks for when it starts that core; God of War and the ND Demo as
  they were. A new shader cache ID once more (KNOWN-ISSUES.md, "Vulkan: Arm's
  driver or malisx2").

- malisx2 0.4.1 (02ca776, Mesa 3b0e036 and the profile as in 1.8; handed
  over by the malisx2 session on 2026-11-11 with the owner's word): upstream's
  0.4.1 merged (buffer device addresses and std430 uniform buffers on v7, a
  shader that reaches a binding its pipeline layout lacks fails to build
  instead of reading past the layout). The speed of ARMSX2, Dolphin and Azahar
  is the same. Its shader cache ID is new, so emulators on malisx2 rebuild
  their pipeline caches once (KNOWN-ISSUES.md, "Vulkan: Arm's driver or
  malisx2").

- Saturn on retrorun and retrorun32 starts again: the launcher the image
  installed called retrorun without its config and without the RG52 Mini's
  profile, and the game did not start (christianhaitian's fix of 2026-11-08,
  ported). DSperate main 0688b41 (v3.4.1 and after: local wireless keeps its
  frame phase), PCSX2's patch database of 2026-11-09, gl4es 1e5d45c; armgba no
  longer offers netplay (TODO.md, "Upstream updates for 1.9").

- armN64, a second low-level Nintendo 64 core (the armGBA author's), offered
  last in the N64 list of EMULATOR SETTINGS; parallel_n64 stays the default.
  Two patches of ours let its GL renderer draw in RetroArch's gl driver:
  Zelda's title at 60 frames a second with a sixth of the CPU; as upstream
  has it the core draws on the CPU here, at 42. Dolphin db2afcb, with two CRT post-processing shaders (crt_pi costs
  2.5% in the ND Demo; `PostProcessingShader = crt_pi` in GFX.ini), armGBA
  v1.5, gl4es 8b1a3da (TODO.md, "Upstream updates for 1.9", the second look).

- Over USB (MTP) a PC sees files and folders whose names start with a dot,
  such as `/roms/.darkos`; up to 1.8 umtp-responder was told to hide them.

- The image's PortMaster installer installs the latest PortMaster (2026.11.12-1059
  in 1.9's image). The installer in PortMaster's release was not rebuilt and installed
  2026.06.23, which knows no newer runtime and cannot read newer ports'
  port.json; the build puts the release's PortMaster.zip into it
  (`scripts/rk3562/pm-installer-update.py`). A card made from an earlier image
  keeps its PortMaster, which lives on the ROMs partition that an update does
  not touch: PortMaster offers its own update when started with Wi-Fi
  (KNOWN-ISSUES.md, "PortMaster: the latest version, and ports copied by hand").

- Ports with PortMaster 2026.11: PortMaster started from Ports runs as ark
  (A/B and X/Y were swapped in its menus), a port that exports the controller
  config itself no longer fails with "Argument list too long" (GTA San
  Andreas), and a port copied again over USB or through Android gets its
  execute bits at every start (`runport.sh`, `portmaster-e2big.sh`; the same
  KNOWN-ISSUES section).

- The Azahar core no longer binds null descriptors on Arm's driver
  (`azahar-patch-034`): a 3DS game that sampled one lost the Vulkan device on
  the stock libmali and RetroArch ended, as Super Mario 3D Land did entering
  world 6-3 (KNOWN-ISSUES.md, "No null descriptors on Arm's driver").

- Options -> Tools: DDR Overclock and LED Settings say "Select + Start to
  Exit" on their exit button.

Checked for 1.9: the release build (20261112155920) on the owner's dual-boot
test card, installed from `ROMs/update` over the test build before it (the
tests before the release were four builds, each put over the last by a
package; the first went on over 1.8). No failed unit, `dpkg --audit` clean,
the files the repo's, the owner's emulator settings kept; then each as ES
starts it, with sound and the right picture: ARMSX2, Dolphin and Azahar on
both Vulkan drivers, DraStic and DSperate, PPSSPP, N64 on parallel_n64 and on
armN64, Dreamcast, Atomiswave, Saturn standalone and on retrorun, Zeebo, GBA
on both cores, SNES, linapple, PS1 on both cores, Sega CD, and the ports Don't
Starve, GTA San Andreas, Postal 2, Doom 3 and Mina the Hollower (TODO.md,
"Upstream updates for 1.9", has the numbers). Super Mario 3D Land's world 6-3
runs on both drivers. The packages from 1.8 and 1.7 were made the same way
from the published images and not installed as released; their one new hook
(`1.9-es-armn64.sh`) was run on the console by hand, twice.

In master for 1.10, not released yet (VERSION says 1.10):

- The screen keeps its brightness over a power-off with the backlight off
  (the sleep hook's power-off after two hours on battery, a power-off under
  ES's black screensaver); up to 1.9 the next boot came up at 5%
  (`darkos-backlight-keep`, KNOWN-ISSUES.md, "The screen at 5% brightness
  after a start").

freej2me-plus, silently missing from earlier images, is built and starts (on
a JDK 11 it downloads), but drew a white, torn picture on the RG52 Mini
(2026-11-03, cause not found); since 1.7 the `freej2me` core on the image's
OpenJDK 21 is offered first (KNOWN-ISSUES.md, "J2ME: the freej2me core on the
system OpenJDK 21"). Yabasanshiro was built and never launched until 2026-10-24, when the one game tried
crashed within seconds, in a dynarec template for an instruction as common as
MOV.B; patches 12 and 13 of its recipe make it run (KNOWN-ISSUES.md).

Not verified: whether the speaker click on playback start and stop is gone.
The GPIO behaves correctly in silence, which is necessary and not sufficient;
only an ear settles it.
