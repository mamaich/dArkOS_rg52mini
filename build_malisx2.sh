#!/bin/bash
#
# Build malisx2 and install it at /usr/lib/aarch64-linux-gnu/malisx2/.  Source:
#
#   https://github.com/mamaich/malisx2-rg52mini  at ${MALISX2_COMMIT} below
#
# That is our fork of https://github.com/bmdhacks/malisx2: our changes are
# commits there (077e853: deviceName "Mali-G52 (malisx2)"; 0488ddb: occlusion
# query pools accepted, every result 0 samples; 80994d9: storage-buffer reads
# in v7 vertex shaders bounds-checked, so Dolphin runs on it; 4c73962-fa1831a:
# fewer compute copy jobs per draw on v7, a stage's tables and FAU block kept
# across pipelines that would build them the same; 9ca29fd-b74141a: v7
# descriptor tables without images and the uniform words filled on the CPU,
# the words copied in runs; bc3a6d1: forward pixel kill kept for draws that
# read no colour attachment under rasterization order access; b511c07: AFBC
# for textures too; d2afec5: the tiler's lowest hierarchy levels on the G52,
# bins of 32 and 128 px as libmali uses, and AFBC for D32 depth attachments;
# 98a8fdf: Index-Driven Vertex Shading on v7, one Indexed Vertex job per draw
# as libmali does, MALISX2_V7_IDVS=0 for the old Vertex + Tiler pair;
# f56aa78-ca3352b: those draws' index ranges scanned by the GPU, compute jobs
# batched per pass as libmali does, not by the CPU through uncached memory,
# MALISX2_V7_GPU_INDEX_RANGE=0 for the CPU scan; 796ca04:
# MALISX2_NO_PIPELINE_CACHE=1, for shader dumps; ce57011-2c219dc: fragment
# shader arithmetic in FP16 where nothing needs 32 bits, MALISX2_V7_FP16=1
# deciding by an estimate of the paired operations, set for ARMSX2 by its
# launcher; 7e4a41d, af9083f: B8G8R8A8 and RGB565 images AFBC-compressed by
# default, MALISX2_V7_AFBC_FMT=0 for neither), and the work on the driver
# itself has its own notes and session
# (T:\Dump\RG52Mini\dArkOS\malisx2\README.md on the owner's PC).
#
# malisx2 is a Vulkan driver for Arm's Mali GPUs on Arm's own kernel driver
# (kbase), made of Mesa's panfrost compiler and Vulkan runtime.  Here it is the
# second driver beside libmali: the Vulkan loader sees only libvulkan_darkos.so
# (build_vkselect.sh), which loads malisx2 for a program when
# /etc/darkos/vulkan.conf or DARKOS_VULKAN asks for it (ES: EMULATOR SETTINGS >
# VULKAN DRIVER).  It compiles pipelines several times faster than libmali (a
# 3DS game's first start) and draws slower (docs/KNOWN-ISSUES.md, "Vulkan:
# Arm's driver or malisx2").
#
# The C and C++ build with the amd64 build of the chroot's clang (xtool.sh),
# which runs natively.  Mesa's Rust compiler for Valhall (kraid) builds with
# the chroot's rustc under qemu: the G52 takes Mesa's Bifrost backend, in C,
# but the build has no switch to leave kraid out.  Meson fetches the Rust
# crates its subprojects/*.wrap name.  RK3562-only.

if [ "$CHIPSET" != "rk3562" ]; then
    return 0
fi

MALISX2_REMOTE="https://github.com/mamaich/malisx2-rg52mini"
# Pinned: a newer commit is a deliberate update (profile retrained, or the
# functions it changed lose their profile data).
MALISX2_COMMIT="d39814144b58994b65d2c020188b6d01708e1bd0"
MALISX2_SRC="Arkbuild/home/ark/malisx2"
MALISX2_LIB="/usr/lib/aarch64-linux-gnu/malisx2/libvulkan_malisx2.so"
# Profile for clang's profile-guided optimisation: trained on 796ca04 on an
# RG52 Mini (2026-10-28): Azahar's Super Mario 3D Land (60 s) and Luigi's
# Mansion (90 s); ARMSX2 started as ES does, 90 s each of God of War from a
# savestate on the ship, Jak and Daxter's intro and Jak and Daxter in game
# from the owner's save. Castlevania is left out of it, to measure with
# (docs/TODO.md). The index range paths that came after it (f56aa78, ca3352b:
# mali_jm_cmd_draw_v7.c, mali_jm_irange_v7.c) are not in it, and clang's
# out-of-date warnings for them are expected. Against the profile of
# 2026-10-27 (of b74141a) on the same code: within the spread. A profile of
# older code loses the functions changed since: 2026-10-25's, of 077e853,
# had no data for fa1831a's draw path, and God of War on fa1831a against
# b1c114a was 0.7% faster with neither profiled, 4.5% with each on its own
# profile. Its file-local functions are named by the path of their source
# (Mesa's reach the compiler by absolute paths), so it is made for this tree
# at /home/ark/malisx2, where the chroot builds it, with the build directory
# in it (malisx2's docs/building.md).
PGO="malisx2/pgo/malisx2-20261028.profdata.gz"
PGO_SHA256="6ccef7c8f2c7e545035cf6f67415f7148b041a0547129acea332e127c8112ef9"
# Meson from its release, run from the tree: Debian trixie's 1.7.0 fails to
# write the build of the Rust crate Mesa makes of structured sources
# ("'CustomTargetIndex' object has no attribute 'subdir'"); 1.12.1 builds it.
MESON_TGZ="meson-1.12.1.tar.gz"
MESON_URL="https://github.com/mesonbuild/meson/releases/download/1.12.1/${MESON_TGZ}"
MESON_SHA256="ab0a6ca09f8ef70c564c8241fb5a23957886a0b53fb58412b5e07eaf07dba743"

if [ ! -d "Arkbuild_package_cache/${CHIPSET}" ]; then
  mkdir -p "Arkbuild_package_cache/${CHIPSET}"
fi

# This recipe and the profile: a change to either rebuilds it
RECIPE_SHA=$(cat build_malisx2.sh ${PGO} | sha1sum | cut -c1-12)
CACHE_KEY="${MALISX2_COMMIT}_${RECIPE_SHA}"

if [ -f "Arkbuild_package_cache/${CHIPSET}/malisx2.tar.gz" ] && \
   [ "$(cat Arkbuild_package_cache/${CHIPSET}/malisx2.commit 2>/dev/null)" == "${CACHE_KEY}" ]; then
    sudo tar -xvzpf "Arkbuild_package_cache/${CHIPSET}/malisx2.tar.gz"
fi

if [ ! -f "Arkbuild${MALISX2_LIB}" ]; then
    if [ "$(sha256sum ${PGO} | cut -c1-64)" != "${PGO_SHA256}" ]; then
        echo "The malisx2 PGO profile ${PGO} is missing or not the one this recipe names.  Stopping here."
        exit 1
    fi

    # Host-side fetch of the one commit
    sudo rm -rf "${MALISX2_SRC}"
    sudo mkdir -p "${MALISX2_SRC}"
    sudo chown "$(id -u):$(id -g)" "${MALISX2_SRC}"
    git -C "${MALISX2_SRC}" init -q
    git -C "${MALISX2_SRC}" remote add origin "${MALISX2_REMOTE}"
    git -C "${MALISX2_SRC}" fetch --depth=1 origin "${MALISX2_COMMIT}"
    verify_action
    git -C "${MALISX2_SRC}" checkout -q --force FETCH_HEAD
    verify_action
    # and Mesa (upstream with bmdhacks' kraid changes) at mesa/, at the commit
    # malisx2's meson.build pins
    MESA_COMMIT=$(sed -n "s/^mesa_commit = '\([0-9a-f]*\)'.*/\1/p" "${MALISX2_SRC}/meson.build")
    git init -q "${MALISX2_SRC}/mesa"
    git -C "${MALISX2_SRC}/mesa" fetch --depth=1 https://github.com/bmdhacks/armsx2-turnip.git "${MESA_COMMIT}"
    verify_action
    git -C "${MALISX2_SRC}/mesa" checkout -q --force FETCH_HEAD
    verify_action

    gunzip -c "${PGO}" > "${MALISX2_SRC}/malisx2.profdata"
    if [ "$(sha256sum Arkbuild_package_cache/${MESON_TGZ} 2>/dev/null | cut -c1-64)" != "${MESON_SHA256}" ]; then
        wget -t 3 -T 60 -O "Arkbuild_package_cache/${MESON_TGZ}" "${MESON_URL}"
    fi
    if [ "$(sha256sum Arkbuild_package_cache/${MESON_TGZ} | cut -c1-64)" != "${MESON_SHA256}" ]; then
        echo "${MESON_TGZ} did not download or has another checksum.  Stopping here."
        exit 1
    fi
    mkdir -p "${MALISX2_SRC}/.meson"
    tar -xzf "Arkbuild_package_cache/${MESON_TGZ}" -C "${MALISX2_SRC}/.meson" --strip-components=1
    sudo chown -R 1000:1000 "${MALISX2_SRC}"

    # bindgen and its libclang for the Rust bindings, mako and yaml for Mesa's
    # generators, ninja, the libraries it links; clang, lld and llvm for xtool
    # (the same clang, amd64), the LTO link and llvm-ar
    call_chroot "dpkg -s bindgen libclang-dev python3-mako python3-yaml ninja-build libdrm-dev libgbm-dev libudev-dev clang lld llvm >/dev/null 2>&1 || (apt-get -y update && eatmydata apt-get -y install bindgen libclang-dev python3-mako python3-yaml ninja-build libdrm-dev libgbm-dev libudev-dev clang lld llvm)"
    verify_action

    # Release (-O3) for the Cortex-A53, full LTO and the profile, linked by lld
    # (it does the LTO): Castlevania's 368 pipelines compile in 7.4 s with LTO
    # alone and in 7.7 s without either (docs/TODO.md).  llvm-ar archives the
    # LTO objects.
    msx_bin=/usr/lib/llvm-19/bin msx_cc=clang msx_cxx=clang++
    if setup_xtool; then
      mount_xtool
      msx_bin=${XTOOL_LLVM}/bin
      msx_cc=${msx_bin}/aarch64-linux-gnu-clang msx_cxx=${msx_bin}/aarch64-linux-gnu-clang++
    fi
    msx_flags="'-mcpu=cortex-a53','-fprofile-use=/home/ark/malisx2/malisx2.profdata','-Wno-profile-instr-unprofiled','-Wno-profile-instr-out-of-date','-Wno-backend-plugin'"
    msx_link="'-fuse-ld=lld','-fprofile-use=/home/ark/malisx2/malisx2.profdata'"
    call_chroot "set -e &&
        cd /home/ark/malisx2 &&
        export CC=${msx_cc} CXX=${msx_cxx} AR=${msx_bin}/llvm-ar &&
        python3 .meson/meson.py setup build-${UNIT} -Dbuildtype=release -Db_lto=true \
            -Dc_args=\"[${msx_flags}]\" -Dcpp_args=\"[${msx_flags}]\" \
            -Dc_link_args=\"[${msx_link}]\" -Dcpp_link_args=\"[${msx_link}]\" &&
        ninja -C build-${UNIT} -j\$(nproc) &&
        mkdir -p $(dirname ${MALISX2_LIB}) &&
        ${msx_bin}/llvm-strip -o ${MALISX2_LIB} build-${UNIT}/src/libvulkan_malisx2.so &&
        chmod 644 ${MALISX2_LIB}
    "
    msx_rc=$?
    umount_xtool
    (exit $msx_rc)
    verify_action
    # the source and the build are not for the image
    sudo rm -rf "${MALISX2_SRC}"

    sudo rm -f "Arkbuild_package_cache/${CHIPSET}/malisx2.tar.gz" "Arkbuild_package_cache/${CHIPSET}/malisx2.commit"
    sudo tar -czpf "Arkbuild_package_cache/${CHIPSET}/malisx2.tar.gz" Arkbuild$(dirname ${MALISX2_LIB})/
    echo "${CACHE_KEY}" > "Arkbuild_package_cache/${CHIPSET}/malisx2.commit"
fi
