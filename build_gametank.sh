#!/bin/bash

# Build and install gametank
# The recipe and its patches are part of the cache key, as in build_fake08.sh:
# a flag changed there (the patch said -mcpu=cortex-a55, whose LSE atomics this
# Cortex-A53 cannot run) would otherwise never reach the image.
# The commit is the recipe's pin (2026-11-15): the key was upstream's head as GitHub's API named it,
# so a commit upstream made between two builds rebuilt the emulator from code nobody had tried, and
# on 2026-11-13 one of those no longer took our patch.
GAMETANK_COMMIT=$(grep -oP '(?<=^commit=")[0-9a-f]+' ${CHIPSET}_core_builds/scripts/gametank.sh)
GAMETANK_RECIPE_SHA=$(cat ${CHIPSET}_core_builds/scripts/gametank.sh ${CHIPSET}_core_builds/patches/gametank-patch* 2>/dev/null | sha1sum | cut -c1-12)
if [ -f "Arkbuild_package_cache/${CHIPSET}/gametank.tar.gz" ] && [ "$(cat Arkbuild_package_cache/${CHIPSET}/gametank.commit)" == "${GAMETANK_COMMIT}-${GAMETANK_RECIPE_SHA}" ]; then
    sudo tar -xvzpf Arkbuild_package_cache/${CHIPSET}/gametank.tar.gz
else
	call_chroot "cd /home/ark &&
	  cd ${CHIPSET}_core_builds &&
	  chmod 777 builds-alt.sh &&
	  ./builds-alt.sh gametank
	  "
	verify_action
	sudo mkdir -p Arkbuild/opt/gametank
	sudo cp -a Arkbuild/home/ark/${CHIPSET}_core_builds/gametank64/GameTankEmulator Arkbuild/opt/gametank/
	if [ -f "Arkbuild_package_cache/${CHIPSET}/gametank.tar.gz" ]; then
	  sudo rm -f Arkbuild_package_cache/${CHIPSET}/gametank.tar.gz
	fi
	if [ -f "Arkbuild_package_cache/${CHIPSET}/gametank.commit" ]; then
	  sudo rm -f Arkbuild_package_cache/${CHIPSET}/gametank.commit
	fi
	if [ -f Arkbuild/opt/gametank/GameTankEmulator ]; then
	  sudo tar -czpf Arkbuild_package_cache/${CHIPSET}/gametank.tar.gz Arkbuild/opt/gametank/
	  echo "${GAMETANK_COMMIT}-${GAMETANK_RECIPE_SHA}" | sudo tee Arkbuild_package_cache/${CHIPSET}/gametank.commit >/dev/null
	fi
fi
sudo cp gametank/config/gametank.gptk Arkbuild/opt/gametank/gametank.gptk
sudo cp gametank/scripts/gametank.sh Arkbuild/usr/local/bin/

call_chroot "chown -R ark:ark /opt/"
sudo chmod 777 Arkbuild/opt/gametank/GameTankEmulator
sudo chmod 777 Arkbuild/usr/local/bin/gametank.sh

