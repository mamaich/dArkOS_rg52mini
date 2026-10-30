#!/bin/bash

# Build and install Retroarch
# For rk3562, use local submodule (bmdhacks fork with rotation patch)
if [ "$CHIPSET" == "rk3562" ]; then
  RETROARCH_TAG=$(grep -oP '(?<=tag=").*?(?=")' rk3562_core_builds/scripts/retroarch.sh)
else
  RETROARCH_TAG=$(curl -s https://raw.githubusercontent.com/christianhaitian/${CORE_BUILDS_CHIPSET}_core_builds/refs/heads/master/scripts/retroarch.sh | grep -oP '(?<=tag=").*?(?=")')
fi
# The cache key: the tag, and on rk3562 a hash of every RetroArch patch the
# build applies as well, so that a new or changed patch rebuilds RetroArch
# instead of restoring a binary built without it.
RETROARCH_KEY="${RETROARCH_TAG}"
if [ "$CHIPSET" == "rk3562" ]; then
  RETROARCH_KEY="${RETROARCH_TAG}_$(cat rk3562_core_builds/patches/retroarch-patch-* retroarch-patches/retroarch-patch-* 2>/dev/null | sha1sum | cut -c1-12)"
fi
# The 32-bit tarball also carries the armhf libSDL2, which only a rebuild of
# RetroArch32 builds again (setup_arkbuild32), so its key covers SDL2's recipe
# and patches too: a change there used to leave the old library in every image
# that restored RetroArch32 from the cache.
RETROARCH32_KEY="${RETROARCH_KEY}"
if [ "$CHIPSET" == "rk3562" ]; then
  RETROARCH32_KEY="${RETROARCH_KEY}_sdl2-$(cat build_sdl2.sh rk3562_core_builds/scripts/sdl2.sh rk3562_core_builds/patches/sdl2-patch-* sdl2-patch-* 2>/dev/null | sha1sum | cut -c1-12)"
fi
if [ -f "Arkbuild_package_cache/${CHIPSET}/retroarch_${UNIT}.tar.gz" ] && [ "$(cat Arkbuild_package_cache/${CHIPSET}/retroarch_${UNIT}.commit)" == "${RETROARCH_KEY}" ]; then
    sudo tar -xvzpf Arkbuild_package_cache/${CHIPSET}/retroarch_${UNIT}.tar.gz
else
	RA_TRIES=0
	while true
	do
	  # Restore core_builds patches to pristine state before device-specific modifications.
	  # Prevents cross-contamination when building multiple devices sequentially.
	  sudo git -C Arkbuild/home/ark/${CHIPSET}_core_builds checkout -- patches/ 2>/dev/null || true
	  # Copy dArkOS-specific RetroArch patches into core_builds patches dir
	  if ls retroarch-patches/retroarch-patch-* 1>/dev/null 2>&1; then
	    echo "Injecting dArkOS RetroArch patches..."
	    sudo cp retroarch-patches/retroarch-patch-* Arkbuild/home/ark/${CHIPSET}_core_builds/patches/
	  fi
	  # Remove rotation patches for landscape-native devices
	  if [ "$CHIPSET" == "rk3562" ] && [ "$UNIT" != "rg52mini" ]; then
	    sudo rm -f Arkbuild/home/ark/${CHIPSET}_core_builds/patches/retroarch-patch-0000-rk3562-rotation-90.patch
	    sudo rm -f Arkbuild/home/ark/${CHIPSET}_core_builds/patches/retroarch-patch-0008-norotation-rga.patch
	    sudo rm -f Arkbuild/home/ark/${CHIPSET}_core_builds/patches/retroarch-patch-0016-rk3562-oga-rotation-90.patch
	    sudo rm -f Arkbuild/home/ark/${CHIPSET}_core_builds/patches/retroarch-patch-0025-rk3562-landscape-output-no-rotation.patch
	  fi
	  call_chroot "cd /home/ark &&
		cd ${CHIPSET}_core_builds &&
		chmod 777 builds-alt.sh &&
		[ -d retroarch ] && rm -rf retroarch* || echo \"Cloning into retroarch\" &&
		eatmydata ./builds-alt.sh retroarch
		"
	  if [[ "$?" -ne "0" ]]; then
		RA_TRIES=$((RA_TRIES+1))
		if [ "$RA_TRIES" -ge 3 ]; then
			echo "ERROR: retroarch step failed $RA_TRIES times in a row; stopping the build"
			echo "       rather than going on without it. See the errors above."
			exit 1
		fi
		sleep 30
		continue
	  else
		break
	  fi
	done

	sudo mkdir -p Arkbuild/opt/retroarch/bin
	sudo mkdir -p Arkbuild/home/ark/.config/retroarch/filters/video
	sudo mkdir -p Arkbuild/home/ark/.config/retroarch/filters/audio
	sudo mkdir -p Arkbuild/home/ark/.config/retroarch/autoconfig/udev
	if [ "$UNIT" == "rgb10" ] || [ "$UNIT" == "rk2020" ]; then
	  sudo cp -a Arkbuild/home/ark/${CHIPSET}_core_builds/retroarch64/retroarch.${CHIPSET}.rot Arkbuild/opt/retroarch/bin/retroarch
	elif [ "$CHIPSET" == "rk3562" ]; then
	  # RK3562: always use retroarch-rgarotated (the first binary produced by builds-alt.sh).
	  # On rg52mini it has rotation patches applied; on landscape devices (rg43h) the rotation
	  # patches are removed so it's a normal unrotated build despite the misleading name.
	  sudo cp -a Arkbuild/home/ark/${CHIPSET}_core_builds/retroarch64/retroarch-rgarotated Arkbuild/opt/retroarch/bin/retroarch
	elif [ "$CHIPSET" == "rk3566" ]; then
	  sudo cp -a Arkbuild/home/ark/${CHIPSET}_core_builds/retroarch64/retroarch Arkbuild/opt/retroarch/bin/retroarch
	else
	  sudo cp -a Arkbuild/home/ark/${CHIPSET}_core_builds/retroarch64/retroarch.${CHIPSET}.unrot Arkbuild/opt/retroarch/bin/retroarch
	fi
	sudo cp -a Arkbuild/home/ark/${CHIPSET}_core_builds/retroarch/gfx/video_filters/*.so Arkbuild/home/ark/.config/retroarch/filters/video/
	sudo cp -a Arkbuild/home/ark/${CHIPSET}_core_builds/retroarch/gfx/video_filters/*.filt Arkbuild/home/ark/.config/retroarch/filters/video/
	sudo cp -a Arkbuild/home/ark/${CHIPSET}_core_builds/retroarch/libretro-common/audio/dsp_filters/*.so Arkbuild/home/ark/.config/retroarch/filters/audio/
	sudo cp -a Arkbuild/home/ark/${CHIPSET}_core_builds/retroarch/libretro-common/audio/dsp_filters/*.dsp Arkbuild/home/ark/.config/retroarch/filters/audio/
	if [ -f "Arkbuild_package_cache/${CHIPSET}/retroarch_${UNIT}.tar.gz" ]; then
	  sudo rm -f Arkbuild_package_cache/${CHIPSET}/retroarch_${UNIT}.tar.gz
	fi
	if [ -f "Arkbuild_package_cache/${CHIPSET}/retroarch_${UNIT}.commit" ]; then
	  sudo rm -f Arkbuild_package_cache/${CHIPSET}/retroarch_${UNIT}.commit
	fi
	sudo tar -czpf Arkbuild_package_cache/${CHIPSET}/retroarch_${UNIT}.tar.gz Arkbuild/opt/retroarch/bin/retroarch Arkbuild/home/ark/.config/retroarch/
	echo "${RETROARCH_KEY}" > Arkbuild_package_cache/${CHIPSET}/retroarch_${UNIT}.commit
fi
sudo rm -rf Arkbuild/home/ark/${CHIPSET}_core_builds/retroarch/
sudo cp retroarch/configs/retroarch.cfg.${UNIT} Arkbuild/home/ark/.config/retroarch/retroarch.cfg
sudo cp retroarch/configs/retroarch.cfg.spectate Arkbuild/home/ark/.config/retroarch/retroarch.cfg.spectate
sudo cp retroarch/configs/retroarch.cfg.vert Arkbuild/home/ark/.config/retroarch/retroarch.cfg.vert
sudo cp retroarch/configs/retroarch.cfg.bak.${UNIT} Arkbuild/home/ark/.config/retroarch/retroarch.cfg.bak
sudo cp retroarch/configs/retroarch-core-options.cfg.${UNIT} Arkbuild/home/ark/.config/retroarch/retroarch-core-options.cfg
sudo cp retroarch/configs/retroarch-core-options.cfg.bak.${UNIT} Arkbuild/home/ark/.config/retroarch/retroarch-core-options.cfg.bak
sudo cp retroarch/configs/controller/*.cfg Arkbuild/home/ark/.config/retroarch/autoconfig/udev/
# Per-core overrides. Azahar (3DS) on the gl driver gets GLES, where the Mali
# driver links each new shader on the emulation thread and the game stalls for
# seconds at a time; on vulkan its pipelines build on worker threads.
if [ "$CHIPSET" == "rk3562" ]; then
  sudo mkdir -p Arkbuild/home/ark/.config/retroarch/config
  sudo cp -r retroarch/configs/config/Azahar Arkbuild/home/ark/.config/retroarch/config/
fi
sudo cp retroarch/scripts/retroarch Arkbuild/usr/local/bin/
sudo mkdir -p Arkbuild/opt/cmds
sudo cp retroarch/scripts/retroarch.sh Arkbuild/opt/cmds
#sudo cp retroarch/scripts/retroarch32.sh Arkbuild/opt/cmds
call_chroot "chown -R ark:ark /opt/"
sudo chmod 777 Arkbuild/opt/cmds/*
sudo chmod 777 Arkbuild/usr/local/bin/retroarch
sudo chmod 777 Arkbuild/opt/retroarch/bin/*
# Add cores requested from retroarch_cores
if [ "$CHIPSET" == "rk3326" ]; then
  CORE_REPO="master"
else
  CORE_REPO="rg503"
fi
ARCH="aarch64"
sudo mkdir -p Arkbuild/home/ark/.config/retroarch/cores
while read RETROARCH_CORE; do
  if [[ ! "$RETROARCH_CORE" =~ ^# ]]; then
    echo -e "Adding ${RETROARCH_CORE} libretro core\n"
    CORE_REF="$CORE_REPO"
    # np2kai on rg503 since 2026-09-07 (NP2kai 5939e0c) leaves ymzadpcm_*
    # undefined and RetroArch cannot open it; take the build before that one.
    if [ "$CORE_REPO" == "rg503" ] && [ "$RETROARCH_CORE" == "np2kai" ]; then
      CORE_REF="e2a8f88be5"
    fi
    wget --retry-connrefused --retry-on-http-error=429 --waitretry=20 -t 65 -T 30 --no-check-certificate https://github.com/christianhaitian/retroarch-cores/raw/"$CORE_REF"/"$ARCH"/"$RETROARCH_CORE"_libretro.so.zip -O /dev/shm/"$RETROARCH_CORE"_libretro.so.zip
    if [ $? -eq 0 ]; then
      sudo unzip -o /dev/shm/"$RETROARCH_CORE"_libretro.so.zip -d Arkbuild/home/ark/.config/retroarch/cores/
      rm -f /dev/shm/"$RETROARCH_CORE"_libretro.so.zip
      printf "\n  ${RETROARCH_CORE} libretro has now been added!\n"
    else
      printf "\n  ${RETROARCH_CORE} libretro was not added!\n"
    fi
    sudo wget --retry-connrefused --retry-on-http-error=429 --waitretry=20 -t 65 -T 30 --no-check-certificate https://github.com/libretro/libretro-core-info/raw/refs/heads/master/"$RETROARCH_CORE"_libretro.info -O Arkbuild/home/ark/.config/retroarch/cores/"$RETROARCH_CORE"_libretro.info
    if [ $? -ne 0 ]; then
      if [ -f "core_info_files/${RETROARCH_CORE}_libretro.info" ]; then
	    sudo cp core_info_files/"$RETROARCH_CORE"_libretro.info Arkbuild/home/ark/.config/retroarch/cores/"$RETROARCH_CORE"_libretro.info
      fi
    fi
  fi
done <retroarch_cores.txt

# Copy other core info files not available from libretro's repo
sudo cp core_info_files/* Arkbuild/home/ark/.config/retroarch/cores/
if [[ "${BUILD_ARMHF}" == "y" ]]; then
  sudo mkdir -p Arkbuild/home/ark/.config/retroarch32/cores
  sudo cp core_info_files/* Arkbuild/home/ark/.config/retroarch32/cores/
fi

# Download and add retroarch assets
sudo git clone --depth=1 https://github.com/libretro/retroarch-assets.git Arkbuild/home/ark/.config/retroarch/assets/
sudo find Arkbuild/home/ark/.config/retroarch/assets/ -maxdepth 1 ! -name assets \
                                                                  ! -name glui \
                                                                  ! -name nxrgui \
                                                                  ! -name ozone \
                                                                  ! -name pkg \
                                                                  ! -name rgui \
                                                                  ! -name sounds \
                                                                  ! -name switch \
                                                                  ! -name xmb \
                                                                  ! -name COPYING -type d,f -not -path '.' -exec rm -rf {} +
# Download and add retroarch shaders
sudo mkdir -p Arkbuild/home/ark/.config/retroarch/shaders/shaders_glsl
sudo git clone --depth=1 https://github.com/libretro/glsl-shaders.git Arkbuild/home/ark/.config/retroarch/shaders/shaders_glsl/
sudo rm -f Arkbuild/home/ark/.config/retroarch/shaders/shaders_glsl/{configure,Makefile}
sudo mkdir -p Arkbuild/home/ark/.config/retroarch/shaders/shaders_glsl/Sharp-Shimmerless
sudo git clone --depth=1 https://github.com/Woohyun-Kang/Sharp-Shimmerless-Shader.git Arkbuild/home/ark/.config/retroarch/shaders/shaders_glsl/Sharp-Shimmerless/
sudo rm -rf Arkbuild/home/ark/.config/retroarch/shaders/shaders_glsl/Sharp-Shimmerless/shaders_slang/
sudo mv Arkbuild/home/ark/.config/retroarch/shaders/shaders_glsl/Sharp-Shimmerless/shaders_glsl/* Arkbuild/home/ark/.config/retroarch/shaders/shaders_glsl/Sharp-Shimmerless/
sudo rm -rf Arkbuild/home/ark/.config/retroarch/shaders/shaders_glsl/Sharp-Shimmerless/shaders_glsl/

# Build libretro easyrpg from scratch since there is usually a need for a matching liblcf file for a new build
# Cache key as in build_scummvmsa.sh: our recipe's pin and a hash of it and its patches.
if [ "$CHIPSET" == "rk3562" ]; then
  EASYRPG_KEY="$(grep -oP '(?<=tag=").*?(?=")' rk3562_core_builds/scripts/easyrpg.sh | head -1)-$(cat rk3562_core_builds/scripts/easyrpg.sh rk3562_core_builds/patches/easyrpg-patch* 2>/dev/null | sha1sum | cut -c1-12)"
else
  EASYRPG_KEY="$(curl -s https://raw.githubusercontent.com/christianhaitian/${CORE_BUILDS_CHIPSET}_core_builds/refs/heads/master/scripts/easyrpg.sh | grep -oP '(?<=tag=").*?(?=")')"
fi
if [ -f "Arkbuild_package_cache/${CHIPSET}/easyrpg.tar.gz" ] && [ "$(cat Arkbuild_package_cache/${CHIPSET}/easyrpg.commit)" = "${EASYRPG_KEY}" ]; then
    sudo tar -xvzpf Arkbuild_package_cache/${CHIPSET}/easyrpg.tar.gz
else
	RA_TRIES=0
	while true
	do
	  call_chroot "cd /home/ark &&
		cd ${CHIPSET}_core_builds &&
		[ -d Player ] && rm -rf Player || echo \"Cloning into Player\" &&
		eatmydata ./builds-alt.sh easyrpg
		"
	  if [[ "$?" -ne "0" ]]; then
		RA_TRIES=$((RA_TRIES+1))
		if [ "$RA_TRIES" -ge 3 ]; then
			echo "ERROR: retroarch step failed $RA_TRIES times in a row; stopping the build"
			echo "       rather than going on without it. See the errors above."
			exit 1
		fi
		sleep 30
		continue
	  else
		break
	  fi
	done
	sudo cp Arkbuild/home/ark/${CHIPSET}_core_builds/cores64/easyrpg_libretro.so Arkbuild/home/ark/.config/retroarch/cores/
	sudo cp Arkbuild/home/ark/${CHIPSET}_core_builds/cores64/liblcf.so.0 Arkbuild/usr/lib/aarch64-linux-gnu/
	if [ -f "Arkbuild_package_cache/${CHIPSET}/easyrpg.tar.gz" ]; then
	  sudo rm -f Arkbuild_package_cache/${CHIPSET}/easyrpg.tar.gz
	fi
	if [ -f "Arkbuild_package_cache/${CHIPSET}/easyrpg.commit" ]; then
	  sudo rm -f Arkbuild_package_cache/${CHIPSET}/easyrpg.commit
	fi
	sudo tar -czpf Arkbuild_package_cache/${CHIPSET}/easyrpg.tar.gz Arkbuild/home/ark/.config/retroarch/cores/easyrpg_libretro.so Arkbuild/usr/lib/aarch64-linux-gnu/liblcf.so.0
	echo "${EASYRPG_KEY}" > Arkbuild_package_cache/${CHIPSET}/easyrpg.commit
fi

# RK3562: build here the cores whose rg503 builds are for the RK3566's
# Cortex-A55 and die on this A53 (inline LSE atomics, see docs/KNOWN-ISSUES.md),
# or are built with no optimisation at all (onscripter), over the downloaded
# ones. Each is cached by its recipe and, unless the recipe pins a commit, the
# upstream head; a failed build keeps the downloaded core and caches nothing.
build_a53_core() {  # builds-alt target, upstream git URL or "pinned", cores it makes...
  local target=$1 url=$2; shift 2
  local recipe key cache files c
  recipe=$(cat ${CHIPSET}_core_builds/scripts/${target}.sh ${CHIPSET}_core_builds/patches/${target}-patch* 2>/dev/null | sha1sum | cut -c1-12)
  key=${recipe}
  [ "$url" != "pinned" ] && key="$(git ls-remote $url HEAD | cut -f1)-${recipe}"
  cache=Arkbuild_package_cache/${CHIPSET}/${target}-a53
  files=""
  for c in "$@"; do files="$files Arkbuild/home/ark/.config/retroarch/cores/${c}_libretro.so"; done
  if [ -f "$cache.tar.gz" ] && [ "$(cat $cache.commit 2>/dev/null)" == "$key" ]; then
    sudo tar -xvzpf $cache.tar.gz
    return
  fi
  # Azahar builds with clang: with its amd64 build when it can (xtool.sh),
  # which runs natively; under qemu the core takes nine and a half hours.
  local xenv=""
  if [ "$target" == "azahar" ]; then
    call_chroot "dpkg -s clang >/dev/null 2>&1 || (apt-get -y update && eatmydata apt-get -y install clang)"
    if setup_xtool; then
      mount_xtool
      xenv="CLANG_CC=${XTOOL_LLVM}/bin/aarch64-linux-gnu-clang CLANG_CXX=${XTOOL_LLVM}/bin/aarch64-linux-gnu-clang++"
    fi
  fi
  call_chroot "cd /home/ark/${CHIPSET}_core_builds && ${xenv} eatmydata ./builds-alt.sh ${target}"
  umount_xtool
  for c in "$@"; do
    if [ ! -f Arkbuild/home/ark/${CHIPSET}_core_builds/cores64/${c}_libretro.so ]; then
      echo "${c} did not build; the image keeps rg503's (see above for what is wrong with it)"
      return
    fi
  done
  for c in "$@"; do
    sudo cp Arkbuild/home/ark/${CHIPSET}_core_builds/cores64/${c}_libretro.so Arkbuild/home/ark/.config/retroarch/cores/
  done
  sudo rm -f $cache.tar.gz $cache.commit
  sudo tar -czpf $cache.tar.gz $files
  echo "$key" | sudo tee $cache.commit >/dev/null
}
if [ "$CHIPSET" == "rk3562" ]; then
  build_a53_core mupen64plus-nx https://github.com/libretro/mupen64plus-libretro-nx.git mupen64plus_next
  build_a53_core flycast pinned flycast flycast_rumble
  build_a53_core puzzlescript https://github.com/nwhitehead/pzretro.git puzzlescript
  build_a53_core samecdi https://github.com/libretro/same_cdi.git same_cdi
  # rg503's is -O0 since upstream dropped its Makefile for a CMakeLists with no
  # build type; onscripter.sh now asks for Release
  build_a53_core onscripter https://github.com/iyzsong/onscripter-libretro.git onscripter
  # Nintendo 3DS: not in rg503's set at all, built here from a pinned release
  build_a53_core azahar pinned azahar
  # and its cheat database: GammaOS's cheats3ds.zip (CTRPF, 60FPS and Sharkive Action Replay
  # codes, one <title id>.txt per game, 752 games), which the core merges into the cheats folder
  # shared with GammaOS when a game starts (azahar-patch-017)
  AZ_CHEATS_URL="https://raw.githubusercontent.com/TheGammaSqueeze/GammaOSNextDistribution-14/dbb613ef825f6f069ce6335c987e2b072b9af926/frameworks/base/cmds/azahar-nano/assets/cheats3ds.zip"
  AZ_CHEATS_SHA256="62efa9af3858a2ebf6a01701846646701bf2f18b3b218408f0a64af9cc01cd38"
  AZ_CHEATS_ZIP=Arkbuild_package_cache/${CHIPSET}/cheats3ds.zip
  if [ "$(sha256sum ${AZ_CHEATS_ZIP} 2>/dev/null | cut -c1-64)" != "${AZ_CHEATS_SHA256}" ]; then
    sudo wget -t 3 -T 60 -O ${AZ_CHEATS_ZIP}.part "${AZ_CHEATS_URL}" && sudo mv ${AZ_CHEATS_ZIP}.part ${AZ_CHEATS_ZIP}
  fi
  if [ "$(sha256sum ${AZ_CHEATS_ZIP} 2>/dev/null | cut -c1-64)" == "${AZ_CHEATS_SHA256}" ]; then
    sudo rm -rf Arkbuild/usr/share/azahar/cheats3ds
    sudo mkdir -p Arkbuild/usr/share/azahar/cheats3ds
    sudo unzip -q -o ${AZ_CHEATS_ZIP} -d Arkbuild/usr/share/azahar/cheats3ds
  else
    echo "cheats3ds.zip did not download or has another checksum; the image ships no 3DS cheats"
  fi
fi

# Build freej2me-lr.jar and freej2me-plus-lr.jar
if [ -f "Arkbuild_package_cache/${CHIPSET}/freej2me-plus.tar.gz" ] && [ "$(cat Arkbuild_package_cache/${CHIPSET}/freej2me-plus.commit)" = "$(curl --silent https://api.github.com/repos/TASEmulators/freej2me-plus/releases | grep '"tag_name":' | head -n 1 | sed -E 's/.*"([^"]+)".*/\1/')" ]; then
    sudo tar -xvzpf Arkbuild_package_cache/${CHIPSET}/freej2me-plus.tar.gz
else
   call_chroot "cd /home/ark &&
		cd ${CHIPSET}_core_builds &&
		[ -d freej2me-plus ] && rm -rf freej2me-plus || echo \"Cloning into freej2me-plus\" &&
		git clone --recursive https://github.com/TASEmulators/freej2me-plus.git &&
		cd freej2me-plus &&
		sed -i '/source\.version\|target\.version/s/value=\"[^\"]*\"/value=\"1.8\"/' build.xml &&
		ant
		"
   sudo mkdir -p Arkbuild/usr/local/bin/freej2me_files/
   # ant builds freej2me_plus-lr.jar, with an underscore; everything downstream
   # wants freej2me-plus-lr.jar, with a hyphen. Rename on the way in rather than
   # patching build.xml, which is where the previous attempt at this went stale.
   sudo cp Arkbuild/home/ark/${CHIPSET}_core_builds/freej2me-plus/build/freej2me_plus-lr.jar \
           Arkbuild/usr/local/bin/freej2me_files/freej2me-plus-lr.jar
   if [ -f "Arkbuild_package_cache/${CHIPSET}/freej2me-plus.tar.gz" ]; then
	  sudo rm -f Arkbuild_package_cache/${CHIPSET}/freej2me-plus.tar.gz
   fi
   if [ -f "Arkbuild_package_cache/${CHIPSET}/freej2me-plus.commit" ]; then
	  sudo rm -f Arkbuild_package_cache/${CHIPSET}/freej2me-plus.commit
   fi
   if sudo test -s Arkbuild/usr/local/bin/freej2me_files/freej2me-plus-lr.jar; then
     sudo tar -czpf Arkbuild_package_cache/${CHIPSET}/freej2me-plus.tar.gz Arkbuild/usr/local/bin/freej2me_files/freej2me-plus-lr.jar
     sudo curl --silent https://api.github.com/repos/TASEmulators/freej2me-plus/releases | grep '"tag_name":' | head -n 1 | sed -E 's/.*"([^"]+)".*/\1/' > Arkbuild_package_cache/${CHIPSET}/freej2me-plus.commit
   else
     echo "freej2me-plus produced no jar - not caching, so the next build retries."
   fi
fi
if [ -f "Arkbuild_package_cache/${CHIPSET}/freej2me.tar.gz" ] && [ "$(cat Arkbuild_package_cache/${CHIPSET}/freej2me.commit)" == "$(curl -s https://api.github.com/repos/hex007/freej2me/commits/master | jq -r '.sha')" ]; then
    sudo tar -xvzpf Arkbuild_package_cache/${CHIPSET}/freej2me.tar.gz
else
   call_chroot "cd /home/ark &&
		cd ${CHIPSET}_core_builds &&
		[ -d freej2me ] && rm -rf freej2me || echo \"Cloning into freej2me\" &&
		git clone --recursive https://github.com/hex007/freej2me.git &&
		cd freej2me &&
		ant
		"
   verify_action
   sudo mkdir -p Arkbuild/usr/local/bin/freej2me_files/
   sudo cp Arkbuild/home/ark/${CHIPSET}_core_builds/freej2me/build/freej2me-lr.jar Arkbuild/usr/local/bin/freej2me_files/
   if [ -f "Arkbuild_package_cache/${CHIPSET}/freej2me.tar.gz" ]; then
	  sudo rm -f Arkbuild_package_cache/${CHIPSET}/freej2me.tar.gz
   fi
   if [ -f "Arkbuild_package_cache/${CHIPSET}/freej2me.commit" ]; then
	  sudo rm -f Arkbuild_package_cache/${CHIPSET}/freej2me.commit
   fi
   sudo tar -czpf Arkbuild_package_cache/${CHIPSET}/freej2me.tar.gz Arkbuild/usr/local/bin/freej2me_files/freej2me-lr.jar
   sudo curl -s https://api.github.com/repos/hex007/freej2me/commits/master | jq -r '.sha' > Arkbuild_package_cache/${CHIPSET}/freej2me.commit
fi

if [[ "${BUILD_ARMHF}" == "y" ]]; then
	if [ -f "Arkbuild_package_cache/${CHIPSET}/retroarch32_${UNIT}.tar.gz" ] && [ "$(cat Arkbuild_package_cache/${CHIPSET}/retroarch32_${UNIT}.commit)" == "${RETROARCH32_KEY}" ]; then
      sudo tar -xvzpf Arkbuild_package_cache/${CHIPSET}/retroarch32_${UNIT}.tar.gz
	else
		setup_arkbuild32
		sudo chroot Arkbuild32/ mkdir -p /home/ark
		RA_TRIES=0
		while true
		do
		  # Restore core_builds patches to pristine state before device-specific modifications.
		  # Prevents cross-contamination when building multiple devices with shared Arkbuild32.
		  sudo git -C Arkbuild32/home/ark/${CHIPSET}_core_builds checkout -- patches/ 2>/dev/null || true
		  # For rk3562 the 32-bit chroot used to get core_builds from a plain
		  # "git clone christianhaitian/rk3566_core_builds" with the rk3562 patches
		  # copied on top.  That leaves the union of two patch sets: the miniloong
		  # patches from upstream touch the same lines of drm_go2_ctx.c as
		  # retroarch-patch-0000-rk3562-rotation-90, so patch reports
		  # "Reversed (or previously applied) patch detected", builds-alt.sh stops,
		  # and the retry loop above span forever.  Use the very same local fork the
		  # 64-bit build uses, so both chroots see one consistent patch set.
		  if [ "$CHIPSET" == "rk3562" ] && [ ! -f "Arkbuild32/home/ark/${CHIPSET}_core_builds/.darkos-local-fork" ]; then
		    echo "Replacing the 32-bit chroot's core_builds with the local rk3562 fork..."
		    sudo rm -rf Arkbuild32/home/ark/${CHIPSET}_core_builds
		    sudo mkdir -p Arkbuild32/home/ark
		    sudo cp -a rk3562_core_builds Arkbuild32/home/ark/${CHIPSET}_core_builds
		    sudo touch Arkbuild32/home/ark/${CHIPSET}_core_builds/.darkos-local-fork
		    sudo chown -R 1000:1000 Arkbuild32/home/ark/${CHIPSET}_core_builds
		  fi
		  # Copy dArkOS-specific RetroArch patches into core_builds patches dir (32-bit)
		  if ls retroarch-patches/retroarch-patch-* 1>/dev/null 2>&1; then
		    echo "Injecting dArkOS RetroArch patches (32-bit)..."
		    sudo cp retroarch-patches/retroarch-patch-* Arkbuild32/home/ark/${CHIPSET}_core_builds/patches/
		  fi
		  # Remove rotation patches for landscape-native devices (32-bit)
		  if [ "$CHIPSET" == "rk3562" ] && [ "$UNIT" != "rg52mini" ]; then
		    sudo rm -f Arkbuild32/home/ark/${CHIPSET}_core_builds/patches/retroarch-patch-0000-rk3562-rotation-90.patch
		    sudo rm -f Arkbuild32/home/ark/${CHIPSET}_core_builds/patches/retroarch-patch-0008-norotation-rga.patch
		    sudo rm -f Arkbuild32/home/ark/${CHIPSET}_core_builds/patches/retroarch-patch-0016-rk3562-oga-rotation-90.patch
		    sudo rm -f Arkbuild32/home/ark/${CHIPSET}_core_builds/patches/retroarch-patch-0025-rk3562-landscape-output-no-rotation.patch
		  fi
		  # 32-bit uses the same GO2_ROTATION_DEGREES_90 as 64-bit.
		  # (Previously reverted to 270 based on incorrect GBM buffer orientation theory.)
		  call_chroot32 "cd /home/ark &&
			if [ ! -d ${CHIPSET}_core_builds ]; then git clone https://github.com/christianhaitian/${CORE_BUILDS_CHIPSET}_core_builds.git ${CHIPSET}_core_builds; fi &&
			cd ${CHIPSET}_core_builds &&
			chmod 777 builds-alt.sh &&
			[ -d retroarch ] && rm -rf retroarch* || echo \"Cloning into retroarch\" &&
			./builds-alt.sh retroarch
			"
		  if [[ "$?" -ne "0" ]]; then
			RA_TRIES=$((RA_TRIES+1))
			if [ "$RA_TRIES" -ge 3 ]; then
				echo "ERROR: retroarch step failed $RA_TRIES times in a row; stopping the build"
				echo "       rather than going on without it. See the errors above."
				exit 1
			fi
			sleep 30
			continue
		  else
			break
		  fi
		done
		sudo mkdir -p Arkbuild/home/ark/.config/retroarch32/filters/video
		sudo mkdir -p Arkbuild/home/ark/.config/retroarch32/filters/audio
		sudo mkdir -p Arkbuild/home/ark/.config/retroarch32/autoconfig/udev
		if [ "$UNIT" == "rgb10" ] || [ "$UNIT" == "rk2020" ]; then
		  sudo cp Arkbuild32/home/ark/${CHIPSET}_core_builds/retroarch32/retroarch32.${CHIPSET}.rot Arkbuild/opt/retroarch/bin/retroarch32
		elif [ "$CHIPSET" == "rk3562" ]; then
		  # RK3562: always use retroarch32-rgarotated (the first binary produced by builds-alt.sh).
		  # On rg52mini it has rotation patches applied; on landscape devices (rg43h) the rotation
		  # patches are removed so it's a normal unrotated build despite the misleading name.
		  sudo cp Arkbuild32/home/ark/${CHIPSET}_core_builds/retroarch32/retroarch32-rgarotated Arkbuild/opt/retroarch/bin/retroarch32
		elif [ "$CHIPSET" == "rk3566" ]; then
		  sudo cp Arkbuild32/home/ark/${CHIPSET}_core_builds/retroarch32/retroarch32 Arkbuild/opt/retroarch/bin/retroarch32
		else
		  sudo cp Arkbuild32/home/ark/${CHIPSET}_core_builds/retroarch32/retroarch32.${CHIPSET}.unrot Arkbuild/opt/retroarch/bin/retroarch32
		fi
		sudo cp -a Arkbuild32/home/ark/${CHIPSET}_core_builds/retroarch/gfx/video_filters/*.so Arkbuild/home/ark/.config/retroarch32/filters/video/
		sudo cp -a Arkbuild32/home/ark/${CHIPSET}_core_builds/retroarch/gfx/video_filters/*.filt Arkbuild/home/ark/.config/retroarch32/filters/video/
		sudo cp -a Arkbuild32/home/ark/${CHIPSET}_core_builds/retroarch/libretro-common/audio/dsp_filters/*.so Arkbuild/home/ark/.config/retroarch32/filters/audio/
		sudo cp -a Arkbuild32/home/ark/${CHIPSET}_core_builds/retroarch/libretro-common/audio/dsp_filters/*.dsp Arkbuild/home/ark/.config/retroarch32/filters/audio/
		if [ -f "Arkbuild_package_cache/${CHIPSET}/retroarch32_${UNIT}.tar.gz" ]; then
	      sudo rm -f Arkbuild_package_cache/${CHIPSET}/retroarch32_${UNIT}.tar.gz
		fi
		if [ -f "Arkbuild_package_cache/${CHIPSET}/retroarch32_${UNIT}.commit" ]; then
	      sudo rm -f Arkbuild_package_cache/${CHIPSET}/retroarch32_${UNIT}.commit
		fi
		# 32-bit RetroArch bundles g13p0 on rk3562 (the 32-bit g29p1 blob crashes); 64-bit uses $whichmali
		if [ "${whichmali_bsp}" == "true" ]; then
		  MALI_LIB="libmali-bifrost-g52-g13p0-gbm.so"
		else
		  MALI_LIB="${whichmali}"
		fi
		sudo tar -czpf Arkbuild_package_cache/${CHIPSET}/retroarch32_${UNIT}.tar.gz Arkbuild/opt/retroarch/bin/retroarch32 Arkbuild/home/ark/.config/retroarch32/ Arkbuild/usr/lib/arm-linux-gnueabihf/libSDL2-2.0.so.0.${extension} Arkbuild/usr/lib/arm-linux-gnueabihf/librga.so* Arkbuild/usr/lib/arm-linux-gnueabihf/libgo2.so* Arkbuild/usr/lib/arm-linux-gnueabihf/${MALI_LIB} Arkbuild/usr/lib/arm-linux-gnueabihf/{libEGL.so,libEGL.so.1,libEGL.so.1.1.0,libGLES_CM.so,libGLES_CM.so.1,libGLESv1_CM.so,libGLESv1_CM.so.1,libGLESv1_CM.so.1.1.0,libGLESv2.so,libGLESv2.so.2,libGLESv2.so.2.0.0,libGLESv2.so.2.1.0,libGLESv3.so,libGLESv3.so.3,libgbm.so,libgbm.so.1,libgbm.so.1.0.0,libmali.so,libmali.so.1,libMaliOpenCL.so,libOpenCL.so,libwayland-egl.so,libwayland-egl.so.1,libwayland-egl.so.1.0.0,libMali.so}
		echo "${RETROARCH32_KEY}" > Arkbuild_package_cache/${CHIPSET}/retroarch32_${UNIT}.commit
	fi
	sudo cp retroarch32/configs/retroarch.cfg.${UNIT} Arkbuild/home/ark/.config/retroarch32/retroarch.cfg
	sudo cp retroarch32/configs/retroarch.cfg.spectate Arkbuild/home/ark/.config/retroarch32/retroarch.cfg.spectate
	sudo cp retroarch32/configs/retroarch.cfg.bak.${UNIT} Arkbuild/home/ark/.config/retroarch32/retroarch.cfg.bak
	sudo cp retroarch32/configs/retroarch-core-options.cfg.${UNIT} Arkbuild/home/ark/.config/retroarch32/retroarch-core-options.cfg
	sudo cp retroarch32/configs/retroarch-core-options.cfg.bak.${UNIT} Arkbuild/home/ark/.config/retroarch32/retroarch-core-options.cfg.bak
	sudo cp retroarch32/configs/controller/*.cfg Arkbuild/home/ark/.config/retroarch32/autoconfig/udev/
	sudo cp retroarch32/scripts/retroarch32 Arkbuild/usr/local/bin/
	sudo cp retroarch32/scripts/retroarch32.sh Arkbuild/opt/cmds
	call_chroot "chown -R ark:ark /opt/"
	sudo chmod 777 Arkbuild/opt/cmds/*
	sudo chmod 777 Arkbuild/usr/local/bin/retroarch32
	sudo chmod 777 Arkbuild/opt/retroarch/bin/*
	# Add cores requested from retroarch_cores32
	if [ "$CHIPSET" == "rk3326" ]; then
	  CORE_REPO="master"
	else
	  CORE_REPO="rg503"
	fi
	ARCH="arm7hf"
	sudo mkdir -p Arkbuild/home/ark/.config/retroarch32/cores
	while read RETROARCH_CORE32; do
	  if [[ ! "$RETROARCH_CORE32" =~ ^# ]]; then
		echo -e "Adding ${RETROARCH_CORE32} libretro core\n"
		wget --retry-connrefused --retry-on-http-error=429 --waitretry=20 -t 65 -T 30 --no-check-certificate https://github.com/christianhaitian/retroarch-cores/raw/"$CORE_REPO"/"$ARCH"/"$RETROARCH_CORE32"_libretro.so.zip -O /dev/shm/"$RETROARCH_CORE32"_libretro.so.zip
		if [ $? -eq 0 ]; then
		  sudo unzip -o /dev/shm/"$RETROARCH_CORE32"_libretro.so.zip -d Arkbuild/home/ark/.config/retroarch32/cores/
		  rm -f /dev/shm/"$RETROARCH_CORE32"_libretro.so.zip
		  printf "\n  ${RETROARCH_CORE32} libretro has now been added!\n"
		else
		  printf "\n  ${RETROARCH_CORE32} libretro was not added!\n"
		fi
		sudo wget --retry-connrefused --retry-on-http-error=429 --waitretry=20 -t 65 -T 30 --no-check-certificate https://github.com/libretro/libretro-core-info/raw/refs/heads/master/"$RETROARCH_CORE32"_libretro.info -O Arkbuild/home/ark/.config/retroarch32/cores/"$RETROARCH_CORE32"_libretro.info
		if [ $? -ne 0 ]; then
		  if [ -f "core_info_files/${RETROARCH_CORE32}_libretro.info" ]; then
			sudo cp core_info_files/"$RETROARCH_CORE32"_libretro.info Arkbuild/home/ark/.config/retroarch32/cores/"$RETROARCH_CORE32"_libretro.info
		  fi
		fi
	  fi
	done <retroarch_cores32.txt

	# Download and add retroarch assets
	sudo git clone --depth=1 https://github.com/libretro/retroarch-assets.git Arkbuild/home/ark/.config/retroarch32/assets/
	sudo find Arkbuild/home/ark/.config/retroarch32/assets/ -maxdepth 1 ! -name assets \
																	  ! -name glui \
																	  ! -name nxrgui \
																	  ! -name ozone \
																	  ! -name pkg \
																	  ! -name rgui \
																	  ! -name sounds \
																	  ! -name switch \
																	  ! -name xmb \
																	  ! -name COPYING -type d,f -not -path '.' -exec rm -rf {} +

	# Download and add retroarch shaders
	sudo mkdir -p Arkbuild/home/ark/.config/retroarch32/shaders/shaders_glsl
	sudo git clone --depth=1 https://github.com/libretro/glsl-shaders.git Arkbuild/home/ark/.config/retroarch32/shaders/shaders_glsl/
	sudo rm -f Arkbuild/home/ark/.config/retroarch32/shaders/shaders_glsl/{configure,Makefile}
	sudo mkdir -p Arkbuild/home/ark/.config/retroarch32/shaders/shaders_glsl/Sharp-Shimmerless
	sudo git clone --depth=1 https://github.com/Woohyun-Kang/Sharp-Shimmerless-Shader.git Arkbuild/home/ark/.config/retroarch32/shaders/shaders_glsl/Sharp-Shimmerless/
	sudo rm -rf Arkbuild/home/ark/.config/retroarch32/shaders/shaders_glsl/Sharp-Shimmerless/shaders_slang/
	sudo mv Arkbuild/home/ark/.config/retroarch32/shaders/shaders_glsl/Sharp-Shimmerless/shaders_glsl/* Arkbuild/home/ark/.config/retroarch32/shaders/shaders_glsl/Sharp-Shimmerless/
	sudo rm -rf Arkbuild/home/ark/.config/retroarch32/shaders/shaders_glsl/Sharp-Shimmerless/shaders_glsl/
fi
call_chroot "chown -R ark:ark /home/ark/.config/"
