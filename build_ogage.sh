#!/bin/bash

# Build and install ogage (Global Hotkey Daemon)
if [ "$UNIT" == "chi" ]; then
  branch="gameforce-chi"
elif [ "$UNIT" == "g350" ]; then
  branch="g350"
elif [ "$UNIT" == "rgb10" ]; then
  branch="master"
elif [ "$UNIT" == "a10mini" ]; then
  branch="a10mini"
elif [ "$UNIT" == "rk2020" ]; then
  branch="rk2020"
elif [ "$UNIT" == "rg351mp" ]; then
  branch="rg351mp"
elif [ "$UNIT" == "rg351v" ]; then
  branch="rg351v"
elif [[ "$UNIT" == *"353"* ]]; then
  branch="rg353v"
elif [[ "$UNIT" == "503" ]] || [[ "$UNIT" == "rg52mini" ]] || [[ "$UNIT" == "rg43h" ]]; then
  branch="rg503"
elif [ "$UNIT" == "rk2023" ] || [ "$UNIT" == "rgb30" ] || [ "$UNIT" == "rgb20pro" ]; then
  branch="rk2023"
fi

# Our patches to the branch (ogage/patches): on the RG52 Mini, BTN_MODE is
# Home, RetroArch's hotkey button, and the rg503 branch toggles Bluetooth on
# it. The RG43V Pro (the rg43h image) has the same button as FN, which its
# H/V selector makes RetroArch's hotkey too; the RG43H has no BTN_MODE, so
# for it the patch changes nothing. The cache holds the branch's commit with
# the patches' hash after it.
ogage_patches=""
if [[ "$UNIT" == "rg52mini" ]] || [[ "$UNIT" == "rg43h" ]]; then
  ogage_patches=$(ls ogage/patches/*.patch)
fi
ogage_suffix=""
if [ -n "$ogage_patches" ]; then
  ogage_suffix="-$(cat $ogage_patches | sha1sum | cut -c1-12)"
fi

if [ -f "Arkbuild_package_cache/${CHIPSET}/ogage_${branch}.tar.gz" ] && [ "$(cat Arkbuild_package_cache/${CHIPSET}/ogage_${branch}.commit)" == "$(curl -s https://api.github.com/repos/christianhaitian/ogage/commits/${branch} | jq -r '.sha')${ogage_suffix}" ]; then
    sudo tar -xvzpf Arkbuild_package_cache/${CHIPSET}/ogage_${branch}.tar.gz
else
	sudo rm -rf Arkbuild/tmp/ogage-patches
	sudo mkdir -p Arkbuild/tmp/ogage-patches
	if [ -n "$ogage_patches" ]; then
	  sudo cp $ogage_patches Arkbuild/tmp/ogage-patches/
	fi
	call_chroot "cd /home/ark &&
	  git clone https://github.com/christianhaitian/ogage.git -b ${branch} &&
	  cd ogage &&
	  for p in /tmp/ogage-patches/*.patch; do [ -f \"\$p\" ] || continue; git apply \"\$p\" || exit 1; done &&
	  export CARGO_NET_GIT_FETCH_WITH_CLI=true &&
	  cargo build --release &&
	  strip target/release/ogage &&
	  cp target/release/ogage /usr/local/bin/ &&
	  chmod 777 /usr/local/bin/ogage
	  "
	verify_action
	if [ -f "Arkbuild_package_cache/${CHIPSET}/ogage.tar.gz" ]; then
	  sudo rm -f Arkbuild_package_cache/${CHIPSET}/ogage_${branch}.tar.gz
	fi
	if [ -f "Arkbuild_package_cache/${CHIPSET}/ogage.commit" ]; then
	  sudo rm -f Arkbuild_package_cache/${CHIPSET}/ogage_${branch}.commit
	fi
	sudo tar -czpf Arkbuild_package_cache/${CHIPSET}/ogage_${branch}.tar.gz Arkbuild/usr/local/bin/ogage
	echo "$(sudo git --git-dir=Arkbuild/home/ark/ogage/.git --work-tree=Arkbuild/home/ark/ogage rev-parse HEAD)${ogage_suffix}" > Arkbuild_package_cache/${CHIPSET}/ogage_${branch}.commit
	sudo rm -rf Arkbuild/home/ark/ogage Arkbuild/tmp/ogage-patches
fi

sudo cp scripts/ogage.service Arkbuild/etc/systemd/system/ogage.service
call_chroot "systemctl enable ogage"
