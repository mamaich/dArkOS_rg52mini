#!/bin/bash

# NPU UPSCALE (npusr/; docs/KNOWN-ISSUES.md, "NPU UPSCALE"): an emulator's native frame - the 3DS's
# top screen, the PSP's - enlarged by a small neural network (npusr/NOTICE) on the RK3562's NPU, inside
# VK_LAYER_DARKOS_rotate (build_vkrotate.sh builds that part). This installs what the layer and the
# launchers need beside it: Rockchip's RKNN runtime (from its release, checked by sha256), the
# models, darkos-npusr (the launchers' helper), the list of systems EmulationStation offers it for,
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
sudo install -m 644 npusr/models/espcn_x3_400x240.rknn npusr/models/espcn_x2_480x272.rknn \
  Arkbuild/usr/share/darkos/npusr/
sudo install -m 755 npusr/darkos-npusr Arkbuild/usr/local/bin/darkos-npusr
sudo install -m 644 npusr/npusr.conf Arkbuild/etc/darkos/npusr.conf
sudo install -m 644 npusr/dArkOS-rknpu.conf Arkbuild/etc/modprobe.d/dArkOS-rknpu.conf
sudo chroot Arkbuild/ ldconfig -X
