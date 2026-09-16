# Hardware fixes

Four patches carried in [mamaich/kernel_rk3562](https://github.com/mamaich/kernel_rk3562),
on top of bmdhacks' `557e5c28a`. They were written and tested against the
Android port of this device and replayed onto this tree; the trees share no
history, so they were applied as patches rather than merged.

    ad4b47f2e  Type-C role switching, speaker click fix, Bluetooth over SDIO
    b5c61cb7d  usb: dwc3: backport the LOA babble filter quirk
    9e1972482  arm64: dts: rg52mini: add the high temperature voltage limit
    db0453bd6  ASoC: rk817: program the ADC sample rate register

## USB host — needs the Type-C controller described

The connector has no ID pin. Role is decided over the CC lines by an HUSB311,
and if the device tree does not describe it, dwc3 stays a peripheral forever:
`/sys/bus/usb/devices/` is empty, without even a root hub.

What has to be there:

* `i2c@ffa10000` enabled, with `husb311@4e`, `compatible = "hynetek,husb311"`,
  interrupt on GPIO0 pin 15, `vbus-supply` pointing at the RK817 `OTG_SWITCH`
  regulator, and a `connector` subnode of type `usb-c-connector` with
  `data-role`/`power-role` dual;
* `usb-role-switch` on `usb@fe500000`. Not extcon — the extcon path from
  usb2-phy waits for an ID pin that does not exist.

Kernel side needs `TYPEC`, `TYPEC_TCPM`, `TYPEC_TCPCI`, `TYPEC_HUSB311` and
`USB_ROLE_SWITCH`; `rg52mini_defconfig` already had all of them, so only the
device tree was missing.

Verified on hardware: plugging in a mouse brings up xHCI and enumerates it.

    xhci-hcd xhci-hcd.1.auto: xHCI Host Controller
    usb 1-1: Product: USB Optical Mouse, Manufacturer: PixArt
    hid-generic 0003:093A:2510.0001: USB HID v1.11 Mouse

`/sys/class/typec/port0/data_role` shows `host [device]` while the port is
connected to a PC and flips when a device is attached — the brackets mark the
current role, so an empty `/sys/bus/usb/devices/` is expected in device mode
and is not a fault.

There is one open loose end: the kernel logs

    OF: graph: no port node found in /i2c@ffa10000/husb311@4e/connector

so the port/endpoint graph between connector and controller is not fully
described, even though role switching works anyway. `backup/dtb/live.dtb`
(dumped from the vendor's EmuELEC) has the complete binding and is the
reference if this ever needs tightening.

## Bluetooth — the driver branch nobody compiled

The AIC8800 driver ships with Bluetooth over SDIO disabled, and the branch
does not build when it is turned on: `CONFIG_SDIO_BT=y` is needed in both
Makefiles, `CONFIG_BLUEDROID` has to be 0 to get the BlueZ path rather than
the Android one, and `hci_dev_get` collides with the kernel's own symbol and
needs renaming. The Android branch (`/dev/aicbt_dev`) is written for kernels
with `CONFIG_BT` off and will not compile here at all.

Verified on hardware:

    hci0: Type: Primary  Bus: SDIO
          BD Address: 0B:3B:22:AC:88:20
          UP RUNNING
    aic8800_fdrv  536576  0
    aic8800_bsp    98304  1 aic8800_fdrv

Module order matters: `aic8800_bsp` first, then `aic8800_fdrv`. The driver
powers the chip itself when it loads, so until the module is up the SDIO bus
looks empty — "SDIO is broken" almost always means "the module did not load".
Bluetooth needs the combo firmware `fmacfwbt_8800d80_h_u02.bin`; the driver
picks it on its own. All 15 D80 blobs ship in `BSP/firmware/aic8800/` and land
at `/lib/firmware/aic8800`, which is what `CONFIG_AIC_FW_PATH` points at.

## Speaker click

The external amplifier is switched by `gpio-115` (`spk-ctl`). The RK817 codec
driver raises it on DAC unmute and drops it on mute, with the delays given by
`spk-mute-delay-ms` and `hp-mute-delay-ms`. Only the headphone delay was in
the tree, so the amplifier switched right on the DAC transition and it was
audible. `spk-mute-delay-ms = <100>` was added.

Check with `cat /sys/kernel/debug/gpio | grep spk`: in silence it should read
`out lo`. It does. Whether the click is actually gone has not been confirmed
by ear.

## Babble filter

A bad cable or static can fake a "babble" condition in the idle window between
EOF2 and the next SOF; xHCI treats it as an error and disables the whole root
port — `usb usb1-port1: disabled by hub (EMI?)`. `GUCTL1.LOA_FILTER_EN` makes
the controller require three consecutive confirmations. This is a backport of
William Wu's commit from `rockchip-linux/kernel`, which landed after this tree
was branched, and it needs both halves: the driver change and
`snps,loa-filter-en-quirk` in the device tree.

Active on the device: `dwc3 fe500000.usb: enable loa filter for port babble`.

## Thermal ceiling

Without `rockchip,high-temp` the system monitor leaves its threshold at
`INT_MAX` and never acts. `rockchip,high-temp = <95000>` and
`rockchip,high-temp-max-volt = <1100000>` are device-tree only; the 5.10
`rockchip_system_monitor` driver already parses both.

This is not the same thing as the GPU throttling described in
[PERFORMANCE.md](PERFORMANCE.md), which is a separate governor and a separate
problem.

## ADC sample rate

`hw_params()` programmed only the DAC rate register, leaving the ADC one set
for 48 kHz. Recording at 16 kHz therefore ran with a misconfigured converter.
Confirmed by reading the register on a live device. It does not affect the
built-in microphone, which is dead in hardware (this reproduces under EmuELEC
with the vendor's own device tree, so it is not a firmware problem), but it
does affect a headset.

## The graphics stack

Read off the blobs and the config rather than from documentation, 2026-09-16.

| | |
|---|---|
| OpenGL ES | 3.2, plus legacy ES-CM 1.1 |
| EGL | 1.5, with the Mali AFBC/AFRC framebuffer-compression extensions |
| Vulkan | 1.3, through the same blob |
| OpenCL | exported by the blob |
| Desktop OpenGL | software only — see below |

The hardware path is ARM's `libmali-bifrost-g52-g29p1.so`, 57 MB, installed as
`libMali.so` with `libEGL`, `libGLESv2`, `libgbm` and friends symlinked onto it
by `build_deps.sh`. Vulkan comes from the same file: it exports
`vk_icdGetInstanceProcAddr`, and `finishing_touches` installs
`BSP/vulkan/rk_vk.json` (`api_version 1.3.276`) pointing at it, a vendor
`libvulkan.so.1.3.274` loader and `vulkaninfo`. `cleanup_filesystem.sh` deletes
the Mesa ICDs so only the Mali one is left.

**There is no hardware desktop OpenGL, and there cannot be.** ARM's blob does
not implement it — no `glBegin`, no GLX. Mesa is installed and provides
`libGL.so.1`, `libGLX_mesa` and a `dri` directory that includes
`panfrost_dri.so`, but panfrost cannot attach to this kernel: the GPU is driven
by ARM's own kbase (`CONFIG_MALI_MIDGARD=y`, the tree that also covers Bifrost)
and `CONFIG_DRM_PANFROST` is not set. So anything that asks for desktop GL gets
llvmpipe, which will report a respectable OpenGL 4.5 and rasterise it on four
Cortex-A53 cores.

The practical rule: an emulator that speaks GLES or Vulkan runs on the GPU, and
one that needs desktop GL will start and be unplayable. That is why the recipes
here pass `USE_EGL=ON` and `USING_FBDEV=ON`.

The 32-bit armhf side is GLES 3.2 as well, but from the older `g13p0` blob.
`build_deps.sh` picks it deliberately: the 32-bit build of g29p1 segfaults
inside libmali during GL and shader setup (SEGV_ACCERR).

## Boot logo

U-Boot draws nothing on this device: the device tree it runs with is the
Rockchip evaluation-board stub, twelve kilobytes with no dsi, panel, vop or
route nodes. Putting `logo.bmp` in the resource partition or on the FAT
partition was tried on the Android port and changed nothing either time.

So the logo comes from the kernel — `CONFIG_LOGO` plus
`BSP/logo_linux_clut224.ppm.gz`, which `build_kernel-rk3562.sh` unpacks into
`drivers/video/logo/` before building. The artwork is the vendor's, lifted
from the EmuELEC boot partition and rotated counter-clockwise to 1280x720 so
that `fbcon=rotate:1` turns it upright.

Two things outside the kernel are required, both in the command line built by
`finishing_touches-rk3562.sh`:

* `loglevel` above 4, because fbcon skips the logo when
  `console_loglevel <= CONSOLE_LOGLEVEL_QUIET`;
* no `console=tty1`, or the boot log is printed over it.

Added after the 2026-09-15 image, so it is not in that build.
