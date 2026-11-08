#!/bin/bash
#
# Build ARMSX2's SDL3 frontend (binary "armsx2-sdl") and install it under
# /opt/pcsx2/.  Source:
#
#   https://github.com/ARMSX2/ARMSX2  at ${ARMSX2_COMMIT} below
#
# ARMSX2 is the PCSX2 fork for ARM64 that the bmdhacks fork built here up to
# 1.3 went into (bmdhacks works on it): the same arm64 recompilers and SDL
# frontend, carried further, and a newer GS with workarounds for Mali drivers.
# On the RG52 Mini it draws Jak and Daxter without the old fork's glitches.
# It builds with clang only (vtlb's handlers, called from the JIT, use
# __attribute__((preserve_most))), so the chroot gets clang, lld and llvm
# (llvm-ar, for LTO) here.
#
# Depends on /opt/sdl3-shim (built by build_sdl3shim.sh) for SDL3 headers
# and libSDL3.so.0.  RK3562-only.

if [ "$CHIPSET" != "rk3562" ]; then
    return 0
fi

ARMSX2_REMOTE="https://github.com/ARMSX2/ARMSX2"
# Pinned: upstream moves every day; a newer commit is a deliberate update
# (patches checked, profile retrained).
# 2.9.2 (ba68927, 2026-11-06), the owner's word of 2026-11-08 (the rule of
# 2026-11-03: taken when no slower). With its profile (below), 3 rounds after
# a warm-up against 1.7's 1049db37: Jak from the owner's savestate 35.7 / 34.5
# / 35.1 against 35.1 / 35.2 / 35.1 (level), Burnout 3 from boot 37.6 / 37.5 /
# 37.0 against 36.5 / 36.8 / 36.9 (+1.7%); with 1.7's profile Jak was 9%
# slower. Our patches apply as they are, and of our PCSX2.ini it reads the
# same keys as 1049db37. Before: master of 2026-11-04 (1049db37, 1.7), from
# the owner's savestates against 1.6's f6df1e69: God of War within the
# spread, Jak +7%, Guitar Hero +1%, Burnout 3 +0.5%.
ARMSX2_COMMIT="ba6892742e357085897d0a151bd8e40485dda968"
PCSX2_SRC="Arkbuild/home/ark/armsx2"
PCSX2_BUILD="/home/ark/armsx2/build-${UNIT}"
# Profile for clang's profile-guided optimisation: 1.7's (below) merged, at
# about the same total count, with the commit above, instrumented with our
# patches, on an RG52 Mini (2026-11-08): Jak (200 s) and the God of War demo
# (120 s) from the owner's savestates, the Burnout 3 demo from boot (120 s).
# clang takes for each function the record that fits its code, so what
# upstream changed gets the new runs. llvm-profdata warns "invalid profile
# created" on the merge, as it did for Guitar Hero's run before; the profile
# reads whole (29276 functions). 1.7's: 1.6's (a7ad453's runs of 2026-10-20/21
# merged with f6df1e69's of 2026-11-03) merged with 1049db37 instrumented,
# from boot: Jak (200 s), Guitar Hero and Burnout 3 (120 s each), 2026-11-05;
# with 1.6's profile alone 1049db37 was 2% slower in God of War and 1.5% in
# Burnout 3.  Its file-local functions are named by the path of their source,
# so it is made for this tree at /home/ark/armsx2, where the chroot builds it.
PGO="pcsx2/pgo/armsx2-20261108.profdata.gz"
PGO_SHA256="adb772c76647ca1e50afe28a84b4f063002d316fe897a89064c75da817c3128a"

if [ ! -d "Arkbuild_package_cache/${CHIPSET}" ]; then
  mkdir -p "Arkbuild_package_cache/${CHIPSET}"
fi

SDL3_SHA=$(cat "Arkbuild_package_cache/${CHIPSET}/sdl3-shim.commit" 2>/dev/null || echo "no-shim")
# This recipe, our fixes (pcsx2/patches) and the profile: a change to any of
# them rebuilds it
RECIPE_SHA=$(cat build_pcsx2sa.sh pcsx2/patches/*.patch ${PGO} | sha1sum | cut -c1-12)
CACHE_KEY="${ARMSX2_COMMIT}_${SDL3_SHA}_${UNIT}_${RECIPE_SHA}"

if [ -f "Arkbuild_package_cache/${CHIPSET}/pcsx2sa_${UNIT}.tar.gz" ] && \
   [ "$(cat Arkbuild_package_cache/${CHIPSET}/pcsx2sa_${UNIT}.commit 2>/dev/null)" == "${CACHE_KEY}" ]; then
    sudo tar -xvzpf "Arkbuild_package_cache/${CHIPSET}/pcsx2sa_${UNIT}.tar.gz"
    if [ ! -f "Arkbuild/opt/pcsx2/bin/armsx2-sdl" ] || \
       ! ls Arkbuild/opt/pcsx2/lib/libplutosvg.so* >/dev/null 2>&1; then
        echo "WARNING: pcsx2sa cache tarball is incomplete, rebuilding from source..."
        sudo rm -f "Arkbuild_package_cache/${CHIPSET}/pcsx2sa_${UNIT}.tar.gz"
        sudo rm -rf Arkbuild/opt/pcsx2
    fi
fi

if [ ! -f "Arkbuild/opt/pcsx2/bin/armsx2-sdl" ]; then
    sudo rm -rf Arkbuild/opt/pcsx2
    if [ "$(sha256sum ${PGO} | cut -c1-64)" != "${PGO_SHA256}" ]; then
        echo "The ARMSX2 PGO profile ${PGO} is missing or not the one this recipe names.  Stopping here."
        exit 1
    fi

    # Host-side fetch of the one commit (HTTPS, no history: upstream has
    # 27k commits)
    sudo rm -rf "${PCSX2_SRC}"
    sudo mkdir -p "${PCSX2_SRC}"
    sudo chown "$(id -u):$(id -g)" "${PCSX2_SRC}"
    git -C "${PCSX2_SRC}" init -q
    git -C "${PCSX2_SRC}" remote add origin "${ARMSX2_REMOTE}"
    git -C "${PCSX2_SRC}" fetch --depth=1 origin "${ARMSX2_COMMIT}"
    verify_action
    git -C "${PCSX2_SRC}" checkout -q --force FETCH_HEAD
    verify_action

    # Debian's libshaderc-dev names the library libshaderc.so / .so.1, which
    # ARMSX2's FindShaderc.cmake does not look for (it loads it by that name
    # at run time already).
    sed -i 's/shaderc_shared.1 shaderc_shared/shaderc_shared.1 shaderc_shared shaderc.1 shaderc/' "${PCSX2_SRC}/cmake/FindShaderc.cmake"

    # Our fixes, on the tree as fetched above (each patch says what it fixes)
    for p in pcsx2/patches/*.patch; do
        patch -d "${PCSX2_SRC}" -Np1 < "$p"
        verify_action
    done
    gunzip -c "${PGO}" > "${PCSX2_SRC}/armsx2.profdata"
    sudo chown -R 1000:1000 "${PCSX2_SRC}"

    # clang, lld and llvm (ARMSX2 builds with clang only; Debian trixie's
    # clang is 19, the profile was made with llvm-profdata 19; CMake archives
    # the LTO part of the core with llvm-ar)
    call_chroot "dpkg -s clang lld llvm >/dev/null 2>&1 || (apt-get -y update && eatmydata apt-get -y install clang lld llvm)"
    verify_action

    # Build plutovg/plutosvg as prereqs — Trixie doesn't ship them and the
    # fork doesn't vendor them.
    call_chroot "set -e &&
        cd /home/ark &&
        if [ ! -d plutovg ]; then
            git clone --depth=1 https://github.com/sammycage/plutovg.git
        fi &&
        cd plutovg && mkdir -p build && cd build &&
        cmake .. -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_INSTALL_PREFIX=/usr/local \
            -DBUILD_SHARED_LIBS=ON \
            -DCMAKE_C_FLAGS='-mcpu=cortex-a53 -O2' &&
        make -j\$(nproc) install &&
        cd /home/ark &&
        if [ ! -d plutosvg ]; then
            git clone --depth=1 https://github.com/sammycage/plutosvg.git
        fi &&
        cd plutosvg && mkdir -p build && cd build &&
        cmake .. -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_INSTALL_PREFIX=/usr/local \
            -DBUILD_SHARED_LIBS=ON \
            -DPLUTOSVG_ENABLE_FREETYPE=ON \
            -DCMAKE_C_FLAGS='-mcpu=cortex-a53 -O2' &&
        make -j\$(nproc) install &&
        ldconfig
    "
    verify_action

    # Configure + build the SDL frontend.
    # - clang with the profile, linking with lld, and LTO_PCSX2_CORE, ARMSX2's
    #   own switch for ThinLTO of the core.  -march=armv8-a: the Cortex-A53
    #   is ARMv8.0, without LSE atomics (ARMSX2 adds its own armv8.1-a only
    #   when no -march is given).  Jak and Daxter's intro on an RG52 Mini,
    #   frames 300-1850, against the same build without the profile and LTO:
    #   +8.6% (31.4 -> 34.1 fps, three series of three runs); the profile
    #   alone +7%.  -mcpu=cortex-a53, -mno-outline-atomics and
    #   -fomit-frame-pointer changed nothing measurable (TODO.md).
    # - SDL3 from /opt/sdl3-shim via PKG_CONFIG_PATH.
    # - HOST_PAGE_SIZE=4096 / HOST_CACHE_LINE_SIZE=64: BuildParameters.cmake
    #   would try_run a probe on the build host; if the chroot ever runs on a
    #   16K-page aarch64 host (e.g. Asahi M2) the detected values get baked
    #   into the binary and it bails with "Page size mismatch" on the 4K-page
    #   RK3562.
    # - X11_API/WAYLAND_API OFF: VK_KHR_display direct-to-monitor is the
    #   only display path needed — no compositor.
    # - RPATH: SDL3 shim and PCSX2's own lib dir.
    # - The resources ninja install copies lack ARMSX2's per-game overrides,
    #   which its build puts next to the binary: copied as the build does.
    # - The compilers: the amd64 build of the chroot's clang when it can
    #   (xtool.sh), which runs natively; under qemu this build takes hours.
    pcsx2_cc=clang pcsx2_cxx=clang++
    if setup_xtool; then
      mount_xtool
      pcsx2_cc=${XTOOL_LLVM}/bin/aarch64-linux-gnu-clang pcsx2_cxx=${XTOOL_LLVM}/bin/aarch64-linux-gnu-clang++
    fi
    flags="-march=armv8-a -moutline-atomics -O3 -fprofile-use=/home/ark/armsx2/armsx2.profdata -Wno-profile-instr-unprofiled -Wno-profile-instr-out-of-date -Wno-backend-plugin"
    call_chroot "set -e &&
        export PKG_CONFIG_PATH=/opt/sdl3-shim/lib/pkgconfig:/usr/local/lib/pkgconfig:\${PKG_CONFIG_PATH} &&
        export LD_LIBRARY_PATH=/opt/sdl3-shim/lib:/usr/local/lib:\${LD_LIBRARY_PATH} &&
        rm -rf ${PCSX2_BUILD} &&
        mkdir -p ${PCSX2_BUILD} &&
        cd ${PCSX2_BUILD} &&
        cmake /home/ark/armsx2 -G Ninja \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_C_COMPILER=${pcsx2_cc} -DCMAKE_CXX_COMPILER=${pcsx2_cxx} \
            -DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld \
            -DPACKAGE_MODE=ON \
            -DCMAKE_INSTALL_PREFIX=/opt/pcsx2 \
            -DENABLE_SDL_FRONTEND=ON \
            -DENABLE_QT_UI=OFF -DENABLE_LIBRETRO=OFF -DENABLE_TESTS=OFF \
            -DUSE_VULKAN=ON -DUSE_OPENGL=ON \
            -DUSE_BACKTRACE=OFF \
            -DX11_API=OFF -DWAYLAND_API=OFF \
            -DHOST_PAGE_SIZE=4096 \
            -DHOST_CACHE_LINE_SIZE=64 \
            -DLTO_PCSX2_CORE=ON \
            -DCMAKE_C_FLAGS='${flags}' \
            -DCMAKE_CXX_FLAGS='${flags}' \
            -DCMAKE_BUILD_RPATH='/opt/sdl3-shim/lib;/opt/pcsx2/lib' \
            -DCMAKE_INSTALL_RPATH='/opt/sdl3-shim/lib;/opt/pcsx2/lib' &&
        ninja -j\$(nproc) &&
        ninja install &&
        strip /opt/pcsx2/bin/armsx2-sdl &&
        cp /home/ark/armsx2/bin/resources-overlay/armsx2_overrides.yaml /opt/pcsx2/share/PCSX2/resources/ &&
        mkdir -p /opt/pcsx2/lib &&
        cp -av /usr/local/lib/libplutovg.so* /usr/local/lib/libplutosvg.so* /opt/pcsx2/lib/
    "
    pcsx2_rc=$?
    umount_xtool
    (exit $pcsx2_rc)
    verify_action
    # the source and the build (several GB) are not for the image
    sudo rm -rf "${PCSX2_SRC}"

    if [ -f "Arkbuild_package_cache/${CHIPSET}/pcsx2sa_${UNIT}.tar.gz" ]; then
      sudo rm -f "Arkbuild_package_cache/${CHIPSET}/pcsx2sa_${UNIT}.tar.gz"
    fi
    if [ -f "Arkbuild_package_cache/${CHIPSET}/pcsx2sa_${UNIT}.commit" ]; then
      sudo rm -f "Arkbuild_package_cache/${CHIPSET}/pcsx2sa_${UNIT}.commit"
    fi
    sudo tar -czpf "Arkbuild_package_cache/${CHIPSET}/pcsx2sa_${UNIT}.tar.gz" Arkbuild/opt/pcsx2/
    echo "${CACHE_KEY}" > "Arkbuild_package_cache/${CHIPSET}/pcsx2sa_${UNIT}.commit"
fi

# PCSX2's game patch database, which ARMSX2 reads as resources/patches.zip: widescreen,
# no-interlace and 60 fps patches and per-game fixes, offered in a game's properties.
# Without it every game start logged "Failed to open patches.zip. Built-in game patches
# are not available." Upstream publishes it only as a moving "latest" release, so it is
# made here as their CI makes it (`7z a -r patches.zip ./patches/*`: the .pnach files at
# the zip's root) from a pinned commit of PCSX2/pcsx2_patches, and kept in the package
# cache under that commit. Outside ARMSX2's own cache, whose key it leaves alone.
PCSX2_PATCHES_COMMIT="192ee9b5d081d23d358c5c9aa6f80eb2ec677fe9"   # 2026-10-31, 4719 patches
PATCHES_ZIP="Arkbuild_package_cache/${CHIPSET}/pcsx2-patches-${PCSX2_PATCHES_COMMIT:0:12}.zip"
if [ ! -s "${PATCHES_ZIP}" ]; then
    rm -rf pcsx2_patches.tmp "${PATCHES_ZIP}.new"
    git init -q pcsx2_patches.tmp &&
    git -C pcsx2_patches.tmp fetch -q --depth 1 https://github.com/PCSX2/pcsx2_patches "${PCSX2_PATCHES_COMMIT}" &&
    git -C pcsx2_patches.tmp checkout -q FETCH_HEAD &&
    (cd pcsx2_patches.tmp/patches &&
     find . -maxdepth 1 -name '*.pnach' -printf '%P\n' | LC_ALL=C sort | zip -q -X -D -@ "${PWD%/pcsx2_patches.tmp/patches}/${PATCHES_ZIP}.new") &&
    [ "$(unzip -Z1 "${PATCHES_ZIP}.new" | grep -c '\.pnach$')" -gt 1000 ] &&
    mv "${PATCHES_ZIP}.new" "${PATCHES_ZIP}"
    verify_action
    rm -rf pcsx2_patches.tmp
fi
sudo install -m 644 "${PATCHES_ZIP}" Arkbuild/opt/pcsx2/share/PCSX2/resources/patches.zip
verify_action

# Install the per-UNIT PCSX2.ini template — the launcher copies it into
# /roms/ps2/pcsx2/inis/PCSX2.ini on first run (PPSSPP convention: all per-
# system writeable data lives on the ROMs partition, not in the rootfs).
sudo mkdir -p Arkbuild/opt/pcsx2/templates
sudo cp "pcsx2/configs/PCSX2.ini.${UNIT}" Arkbuild/opt/pcsx2/templates/PCSX2.ini

sudo cp pcsx2/scripts/standalone-pcsx2sa Arkbuild/usr/local/bin/standalone-pcsx2sa
sudo chmod 755 Arkbuild/usr/local/bin/standalone-pcsx2sa

call_chroot "chown -R ark:ark /opt/pcsx2"
sudo chmod 755 Arkbuild/opt/pcsx2/bin/armsx2-sdl
