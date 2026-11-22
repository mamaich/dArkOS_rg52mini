#!/bin/bash

# Build and install Dolphin standalone emulator
# Cache key: hash of the dolphinsa recipe AND its patches (the recipe copies the
# patches from the working tree, so a patch-only change must bust the cache too).
# Keyed per-UNIT as it has been (the Vulkan rotation patch that made the
# binaries differ went with 2609a: the rotate layer turns Dolphin's swapchain).
# The profile for clang's profile-guided optimisation (pgo/dolphin-*) counts too.
DOLPHINSA_RECIPE_SHA=$(cat rk3562_core_builds/scripts/dolphinsa.sh rk3562_core_builds/patches/dolphinsa-patch* rk3562_core_builds/pgo/dolphin-*.profdata.gz 2>/dev/null | sha1sum | awk '{print $1}')
if [ -f "Arkbuild_package_cache/${CHIPSET}/dolphinsa_${UNIT}.tar.gz" ] && \
   [ "$(cat Arkbuild_package_cache/${CHIPSET}/dolphinsa_${UNIT}.commit 2>/dev/null)" == "${DOLPHINSA_RECIPE_SHA}" ]; then
    sudo tar -xvzpf Arkbuild_package_cache/${CHIPSET}/dolphinsa_${UNIT}.tar.gz
    # Validate cache extraction produced the expected binary
    if [ ! -f "Arkbuild/opt/dolphin/dolphin-emu-nogui" ]; then
        echo "WARNING: Dolphin cache tarball is corrupt, rebuilding from source..."
        sudo rm -f Arkbuild_package_cache/${CHIPSET}/dolphinsa_${UNIT}.tar.gz
    fi
fi
if [ ! -f "Arkbuild/opt/dolphin/dolphin-emu-nogui" ]; then
	# Dolphin builds with clang (1.13): with its amd64 build when it can (xtool.sh), which runs
	# natively; the recipe falls back to the chroot's clang under qemu.
	xenv=""
	call_chroot "dpkg -s clang lld llvm >/dev/null 2>&1 || (apt-get -y update && eatmydata apt-get -y install clang lld llvm)"
	if setup_xtool; then
	  mount_xtool
	  xenv="CLANG_CC=${XTOOL_LLVM}/bin/aarch64-linux-gnu-clang CLANG_CXX=${XTOOL_LLVM}/bin/aarch64-linux-gnu-clang++"
	fi
	call_chroot "cd /home/ark &&
	  cd ${CHIPSET}_core_builds &&
	  chmod 777 builds-alt.sh &&
	  ${xenv} eatmydata ./builds-alt.sh dolphinsa
	  "
	verify_action
	umount_xtool
	sudo mkdir -p Arkbuild/opt/dolphin
	sudo cp -Ra Arkbuild/home/ark/${CHIPSET}_core_builds/dolphinsa64/dolphin-emu-nogui Arkbuild/opt/dolphin/
	# Dolphin's own data (GameSettings, codehandler.bin for Gecko codes, the
	# IPL fonts, shaders...) beside the binary, where 2609a looks for it first.
	# Before 2609a it was copied into the user directory instead, where it
	# overrides what Dolphin ships as if the user had written it; an update
	# deletes those copies the user did not change (darkos-ota).
	sudo cp -Ra Arkbuild/home/ark/${CHIPSET}_core_builds/dolphin/Data/Sys Arkbuild/opt/dolphin/
	sudo rm -f Arkbuild_package_cache/${CHIPSET}/dolphinsa_${UNIT}.tar.gz Arkbuild_package_cache/${CHIPSET}/dolphinsa_${UNIT}.commit
	sudo tar -czpf Arkbuild_package_cache/${CHIPSET}/dolphinsa_${UNIT}.tar.gz Arkbuild/opt/dolphin/
	echo "${DOLPHINSA_RECIPE_SHA}" | sudo tee Arkbuild_package_cache/${CHIPSET}/dolphinsa_${UNIT}.commit > /dev/null
fi
# Our settings for games Dolphin ships none for (KNOWN-ISSUES.md, "Dolphin: Soulcalibur II hung"):
# into its own GameSettings, beside upstream's, so a user's own GameSettings still win.
if [ -d "dolphin/configs/${CHIPSET}/GameSettings" ]; then
  sudo cp dolphin/configs/${CHIPSET}/GameSettings/*.ini Arkbuild/opt/dolphin/Sys/GameSettings/
fi
sudo mkdir -p Arkbuild/home/ark/.local/share/dolphin-emu
sudo cp -R dolphin/Config/ Arkbuild/home/ark/.local/share/dolphin-emu/
# rk3562 devices have a different built-in gamepad (evdev name "rk3562-joystick", not the
# rk3566 "retrogame_joypad"), so the shared GCPadNew.ini binds to nothing -> no input.
# Ship a chipset-specific GCPadNew.ini (correct Device name; GC Z on the R2 trigger axis
# since our R2 is an axis, not a button). Same pad on all rk3562 units, so keyed on CHIPSET.
if [ -f "dolphin/configs/${CHIPSET}/GCPadNew.ini" ]; then
  sudo cp dolphin/configs/${CHIPSET}/GCPadNew.ini Arkbuild/home/ark/.local/share/dolphin-emu/Config/GCPadNew.ini
fi
# The same for the Wii (1.13): an emulated Wiimote with a Nunchuk on the built-in pad (the shared
# WiimoteNew.ini is empty, so Wii games took no input). Wii games run from /roms/gc.
if [ -f "dolphin/configs/${CHIPSET}/WiimoteNew.ini" ]; then
  sudo cp dolphin/configs/${CHIPSET}/WiimoteNew.ini Arkbuild/home/ark/.local/share/dolphin-emu/Config/WiimoteNew.ini
fi
sudo cp dolphin/scripts/dolphin.sh Arkbuild/usr/local/bin/
call_chroot "chown -R ark:ark /opt/"
call_chroot "chown -R ark:ark /home/ark/"
sudo chmod 777 Arkbuild/opt/dolphin/dolphin-emu-nogui
sudo chmod 777 Arkbuild/usr/local/bin/dolphin.sh
