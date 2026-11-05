#!/bin/bash

# Build and install linapple standalone emulator (on SDL2 since rk3566_core_builds' f24bc2a)
if [ -f "Arkbuild_package_cache/${CHIPSET}/linapplesa.tar.gz" ] && [ "$(cat Arkbuild_package_cache/${CHIPSET}/linapplesa.commit)" == "$(curl -s https://api.github.com/repos/christianhaitian/linapple/commits/master | jq -r '.sha')" ]; then
    sudo tar -xvzpf Arkbuild_package_cache/${CHIPSET}/linapplesa.tar.gz
else
	# linapple's build needs imagemagick (it turns the icons into headers, and pulls in
	# ghostscript, netpbm and their fonts: 36 packages, ~40 MB). The binary needs none of
	# them (libzip, libcurl and SDL2_image are in the image anyway), so what the build
	# added is purged after it.
	call_chroot "dpkg-query -W -f '\${Package}\n' | sort > /tmp/linapple-pkgs-before"
	# Since rk3566_core_builds' f24bc2a linapple builds on SDL2 (no sdl12-compat), and its
	# recipe installs nothing: imagemagick (for the icons) is put in here, inside the window
	# whose packages are purged below, as upstream dArkOS has it in needed_dev_packages.txt.
	call_chroot "export DEBIAN_FRONTEND=noninteractive && apt-get -y install imagemagick"
	verify_action
	call_chroot "export CCACHE_DISABLE=1 &&
	  cd /home/ark &&
	  cd ${CHIPSET}_core_builds &&
	  chmod 777 builds-alt.sh &&
	  eatmydata ./builds-alt.sh linapplesa
	  "
	verify_action
	call_chroot "dpkg-query -W -f '\${Package}\n' | sort > /tmp/linapple-pkgs-after &&
	  added=\$(comm -13 /tmp/linapple-pkgs-before /tmp/linapple-pkgs-after) &&
	  if [ -n \"\$added\" ]; then echo linapple build packages purged: \$added; apt-get -y purge \$added; fi &&
	  rm -f /tmp/linapple-pkgs-before /tmp/linapple-pkgs-after"
	verify_action
	sudo mkdir -p Arkbuild/opt/linapple
	sudo cp -a Arkbuild/home/ark/${CHIPSET}_core_builds/linapplesa-64/linapple Arkbuild/opt/linapple/
	sudo cp -a Arkbuild/home/ark/${CHIPSET}_core_builds/linapple/res/Master.dsk Arkbuild/opt/linapple/
	sudo cp -a Arkbuild/home/ark/${CHIPSET}_core_builds/linapple/res/A2_BASIC.SYM Arkbuild/opt/linapple/
	sudo cp -a Arkbuild/home/ark/${CHIPSET}_core_builds/linapple/res/APPLE2E.SYM Arkbuild/opt/linapple/
	if [ -f "Arkbuild_package_cache/${CHIPSET}/linapplesa.tar.gz" ]; then
	  sudo rm -f Arkbuild_package_cache/${CHIPSET}/linapplesa.tar.gz
	fi
	if [ -f "Arkbuild_package_cache/${CHIPSET}/linapplesa.commit" ]; then
	  sudo rm -f Arkbuild_package_cache/${CHIPSET}/linapplesa.commit
	fi
	sudo tar -czpf Arkbuild_package_cache/${CHIPSET}/linapplesa.tar.gz Arkbuild/opt/linapple/
	sudo git --git-dir=Arkbuild/home/ark/${CHIPSET}_core_builds/linapple/.git --work-tree=Arkbuild/home/ark/${CHIPSET}_core_builds/linapple rev-parse HEAD > Arkbuild_package_cache/${CHIPSET}/linapplesa.commit
fi

sudo cp linapple/gamecontrollerdb.txt Arkbuild/opt/linapple/
sudo cp -R linapple/configs/* Arkbuild/opt/linapple/
sudo cp linapple/apple2.sh Arkbuild/usr/local/bin/
call_chroot "chown -R ark:ark /opt/"
sudo chmod 777 Arkbuild/opt/linapple/linapple
sudo chmod 777 Arkbuild/usr/local/bin/apple2.sh