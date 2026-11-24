#!/bin/bash

# NPU UPSCALE (npusr/; docs/KNOWN-ISSUES.md, "NPU UPSCALE"): an emulator's native frame - the 3DS's
# top screen, the PSP's - enlarged by a small neural network (npusr/NOTICE) on the RK3562's NPU, inside
# VK_LAYER_DARKOS_rotate (build_vkrotate.sh builds that part). This installs what the layer and the
# launchers need beside it: Rockchip's RKNN runtime (from its release, checked by sha256; aarch64 and
# armhf), the models, libnpusr_gles.so (the same for PS1's GLES in the 32-bit RetroArch, npusr-gles/),
# darkos-npusr (the launchers' helper), the list of systems EmulationStation offers it for,
# the blacklist that keeps rknpu.ko from loading at boot, and the notice and licenses of all that.
# The kernel side (the NPU in the dts, rknpu as a module) is in kernel_rk3562.
RKNNRT_URL="https://raw.githubusercontent.com/airockchip/rknn-toolkit2/v2.5.0/rknpu2/runtime/Linux/librknn_api/aarch64/librknnrt.so"
RKNNRT_SHA256="4b6cc575bedf8952b13db4b1190fa3b3d2ac213f00be774ff6ccd19d651970c0"
rknnrt="Arkbuild_package_cache/${CHIPSET}/librknnrt-2.5.0.so"
if [ "$(sha256sum "$rknnrt" 2>/dev/null | cut -d' ' -f1)" != "$RKNNRT_SHA256" ]; then
  mkdir -p "Arkbuild_package_cache/${CHIPSET}"
  curl -sSfL --retry 3 -o "$rknnrt.part" "$RKNNRT_URL" && mv -f "$rknnrt.part" "$rknnrt"
fi
if [ "$(sha256sum "$rknnrt" 2>/dev/null | cut -d' ' -f1)" != "$RKNNRT_SHA256" ]; then
  echo "ERROR: librknnrt.so 2.5.0 could not be fetched, or its sha256 is not ${RKNNRT_SHA256} (${RKNNRT_URL})"
  exit 1
fi
sudo install -m 644 "$rknnrt" Arkbuild/usr/lib/aarch64-linux-gnu/librknnrt.so
sudo mkdir -p Arkbuild/usr/share/doc/librknnrt Arkbuild/usr/share/doc/darkos-npusr \
  Arkbuild/usr/share/darkos/npusr Arkbuild/etc/darkos Arkbuild/etc/modprobe.d
sudo install -m 644 npusr/LICENSE.rknn Arkbuild/usr/share/doc/librknnrt/LICENSE
sudo install -m 644 npusr/NOTICE npusr/models/LICENSE-TF-ESPCN Arkbuild/usr/share/doc/darkos-npusr/
sudo install -m 644 npusr/models/espcn_x3_400x240.rknn npusr/models/espcn_x3_480x272.rknn \
  npusr/models/espcn_x2_480x272.rknn Arkbuild/usr/share/darkos/npusr/
sudo install -m 755 npusr/darkos-npusr Arkbuild/usr/local/bin/darkos-npusr
sudo install -m 644 npusr/npusr.conf Arkbuild/etc/darkos/npusr.conf
sudo install -m 644 npusr/dArkOS-rknpu.conf Arkbuild/etc/modprobe.d/dArkOS-rknpu.conf

# The same for PS1 in the 32-bit RetroArch (pcsx_rearmed; GLES, past the Vulkan layer): Rockchip's armhf
# runtime, libnpusr_gles.so (npusr-gles/, preloaded by retroarch32) built in the armhf chroot, and the
# models for PS1's frame sizes (npusr-gles/README.md).
RKNNRT32_URL="https://raw.githubusercontent.com/airockchip/rknn-toolkit2/v2.5.0/rknpu2/runtime/Linux/librknn_api/armhf/librknnrt.so"
RKNNRT32_SHA256="267193f5f32f01f6e9137072c67ca737d57528257f1a63ce4d65e3698369ffa3"
rknnrt32="Arkbuild_package_cache/${CHIPSET}/librknnrt-2.5.0-armhf.so"
if [ "$(sha256sum "$rknnrt32" 2>/dev/null | cut -d' ' -f1)" != "$RKNNRT32_SHA256" ]; then
  curl -sSfL --retry 3 -o "$rknnrt32.part" "$RKNNRT32_URL" && mv -f "$rknnrt32.part" "$rknnrt32"
fi
if [ "$(sha256sum "$rknnrt32" 2>/dev/null | cut -d' ' -f1)" != "$RKNNRT32_SHA256" ]; then
  echo "ERROR: the armhf librknnrt.so 2.5.0 could not be fetched, or its sha256 is not ${RKNNRT32_SHA256} (${RKNNRT32_URL})"
  exit 1
fi
sudo mkdir -p Arkbuild/usr/lib/arm-linux-gnueabihf
sudo install -m 644 "$rknnrt32" Arkbuild/usr/lib/arm-linux-gnueabihf/librknnrt.so
sudo rm -rf Arkbuild32/home/ark/npusr-gles
sudo mkdir -p Arkbuild32/home/ark/npusr-gles
sudo cp npusr-gles/npusr_gles.c npusr-gles/build.sh vk-rotate-layer/rknn_api.h Arkbuild32/home/ark/npusr-gles/
call_chroot32 "cd /home/ark/npusr-gles &&
  { apt-get download libegl-dev libgles-dev libgl-dev ||
    { apt-get -y update && apt-get download libegl-dev libgles-dev libgl-dev; }; } &&
  dpkg -x libegl-dev_*.deb sys && dpkg -x libgles-dev_*.deb sys && dpkg -x libgl-dev_*.deb sys &&
  SYSINC=sys/usr/include sh build.sh"
if [ ! -f Arkbuild32/home/ark/npusr-gles/libnpusr_gles.so ]; then
  echo "ERROR: libnpusr_gles.so was not built (npusr-gles/)"
  exit 1
fi
sudo install -m 644 Arkbuild32/home/ark/npusr-gles/libnpusr_gles.so Arkbuild/usr/lib/arm-linux-gnueabihf/libnpusr_gles.so
sudo rm -rf Arkbuild32/home/ark/npusr-gles
sudo install -m 644 npusr/models/x3_256x240.rknn npusr/models/x3_320x240.rknn npusr/models/x3_368x240.rknn \
  npusr/models/x3_256x224.rknn npusr/models/x3_320x224.rknn npusr/models/x3_368x224.rknn \
  Arkbuild/usr/share/darkos/npusr/
sudo install -m 644 npusr-gles/README.md Arkbuild/usr/share/doc/darkos-npusr/README-gles.md
sudo chroot Arkbuild/ ldconfig -X
