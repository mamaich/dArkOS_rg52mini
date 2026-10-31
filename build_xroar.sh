#!/bin/bash

# Build and install XRoar
# Cache key as in build_scummvmsa.sh: our recipe's pin and a hash of it and its patches.
if [ "$CHIPSET" == "rk3562" ]; then
  XROAR_KEY="$(grep -oP '(?<=tarname=").*?(?=")' rk3562_core_builds/scripts/xroar.sh | head -1)-$(cat rk3562_core_builds/scripts/xroar.sh rk3562_core_builds/patches/xroar-patch* 2>/dev/null | sha1sum | cut -c1-12)"
else
  XROAR_KEY="$(curl -s https://raw.githubusercontent.com/christianhaitian/${CORE_BUILDS_CHIPSET}_core_builds/refs/heads/master/scripts/xroar.sh | grep -oP '(?<=tarname=").*?(?=")')"
fi
if [ -f "Arkbuild_package_cache/${CHIPSET}/xroar.tar.gz" ] && [ "$(cat Arkbuild_package_cache/${CHIPSET}/xroar.commit)" == "${XROAR_KEY}" ]; then
    sudo tar -xvzpf Arkbuild_package_cache/${CHIPSET}/xroar.tar.gz
else
	call_chroot "cd /home/ark &&
	  cd ${CHIPSET}_core_builds &&
	  chmod 777 builds-alt.sh &&
	  ./builds-alt.sh xroar
	  "
	verify_action
	sudo mkdir -p Arkbuild/opt/xroar
	sudo cp -a Arkbuild/home/ark/${CHIPSET}_core_builds/xroar64/xroar Arkbuild/opt/xroar/
	if [ -f "Arkbuild_package_cache/${CHIPSET}/xroar.tar.gz" ]; then
	  sudo rm -f Arkbuild_package_cache/${CHIPSET}/xroar.tar.gz
	fi
	if [ -f "Arkbuild_package_cache/${CHIPSET}/xroar.commit" ]; then
	  sudo rm -f Arkbuild_package_cache/${CHIPSET}/xroar.commit
	fi
	sudo tar -czpf Arkbuild_package_cache/${CHIPSET}/xroar.tar.gz Arkbuild/opt/xroar/
	echo "${XROAR_KEY}" > Arkbuild_package_cache/${CHIPSET}/xroar.commit
fi

sudo cp -a xroar/coco.sh Arkbuild/usr/local/bin/
# ES's Dragon 32 system starts dragon.sh; until 1.5 it was not installed
sudo cp -a xroar/dragon.sh Arkbuild/usr/local/bin/
sudo cp -R xroar/controls/ Arkbuild/opt/xroar/
sudo cp -a xroar/xroar.gptk Arkbuild/opt/xroar/

call_chroot "chown -R ark:ark /opt/"
sudo chmod 777 Arkbuild/opt/xroar/xroar
sudo chmod 777 Arkbuild/usr/local/bin/coco.sh
sudo chmod 777 Arkbuild/usr/local/bin/dragon.sh

