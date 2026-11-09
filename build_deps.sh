#!/bin/bash

echo -e "Installing build dependencies and needed packages...\n\n"

if [ "$1" == "32" ]; then
  BIT="32"
  ARCH="arm-linux-gnueabihf"
  CHROOT_DIR="Arkbuild32"
else
  BIT="64"
  ARCH="aarch64-linux-gnu"
  CHROOT_DIR="Arkbuild"
fi

# Bind ccache to chroot to speed up consecutive builds
[ ! -d "${CHROOT_DIR}/home/ark/Arkbuild_ccache" ] && sudo mkdir -p ${CHROOT_DIR}/home/ark/Arkbuild_ccache
sudo mount --bind ${PWD}/Arkbuild_ccache ${CHROOT_DIR}/home/ark/Arkbuild_ccache

# The rest, in the 64-bit chroot, comes from Arkbuild_package_cache when
# nothing it is made of has changed (rootfs_cache.sh): under qemu it takes
# over three hours, nearly all of it apt.
if [ "$BIT" == "64" ]; then
  if restore_rootfs_cache deps; then
    return 0
  fi
  rootfs_delta_begin
fi

# Install additional needed packages and protect them from autoremove
while read NEEDED_PACKAGE; do
  if [[ ! "$NEEDED_PACKAGE" =~ ^# ]]; then
    install_package $BIT "${NEEDED_PACKAGE}"
    protect_package $BIT "${NEEDED_PACKAGE}"
  fi
done <needed_packages.txt

# Install build dependencies
while read NEEDED_DEV_PACKAGE; do
  if [[ ! "$NEEDED_DEV_PACKAGE" =~ ^# ]]; then
    install_package $BIT "${NEEDED_DEV_PACKAGE}"
    #protect_package $BIT "${NEEDED_DEV_PACKAGE}"
  fi
done <needed_dev_packages.txt

# Default gcc and g++ to version 12 if gcc is newer than 12
GCC_VERSION=`sudo chroot ${CHROOT_DIR}/ bash -c "gcc --version | head -n 1 | awk '{print $3}' | cut -d' ' -f3 | cut -d'.' -f1"`
if (( GCC_VERSION > 12 )); then
  install_package $BIT gcc-12
  install_package $BIT g++-12
  sudo chroot ${CHROOT_DIR}/ bash -c "update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-12 10"
  sudo chroot ${CHROOT_DIR}/ bash -c "update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-12 20"
  sudo chroot ${CHROOT_DIR}/ bash -c "update-alternatives --set gcc /usr/bin/gcc-12"
  sudo chroot ${CHROOT_DIR}/ bash -c "update-alternatives --set g++ /usr/bin/g++-12"
fi

# Point the chroot's root at the ccache mounted above
sudo chroot ${CHROOT_DIR}/ bash -c "[ -z \$(echo \$CCACHE_DIR | grep ccache) ]" && echo -e "export CCACHE_DIR=/home/ark/Arkbuild_ccache" | sudo tee -a ${CHROOT_DIR}/root/.bashrc > /dev/null
sudo chroot ${CHROOT_DIR}/ bash -c "[ -z \$(echo \$PATH | grep ccache) ]" && echo -e "export PATH=/usr/lib/ccache:\$PATH" | sudo tee -a ${CHROOT_DIR}/root/.bashrc > /dev/null
sudo chroot ${CHROOT_DIR}/ bash -c "/usr/sbin/update-ccache-symlinks"

# Symlink fix for DRM headers
sudo chroot ${CHROOT_DIR}/ bash -c "ln -s /usr/include/libdrm/ /usr/include/drm"

# Place libmali manually (assumes you have libmali.so or mali drivers ready)
# For rk3562, Mali libs are pre-installed from BSP in build_kernel-rk3562.sh
ARCHITECTURE_ARRAY=("aarch64-linux-gnu")
if [[ "${BUILD_ARMHF}" == "y" ]]; then
  ARCHITECTURE_ARRAY+=("arm-linux-gnueabihf")
fi
for ARCHITECTURE in "${ARCHITECTURE_ARRAY[@]}"
do
  if [ "$ARCHITECTURE" == "aarch64-linux-gnu" ]; then
    FOLDER="aarch64"
  else
    FOLDER="armhf"
  fi
  sudo mkdir -p Arkbuild/usr/lib/${ARCHITECTURE}/

  # 64-bit uses g29p1 (BSP); 32-bit armhf stays on g13p0 from core_builds. The 32-bit
  # g29p1 crashed in its GLSL ES compiler (it reads 64 KiB below its data, in the hole
  # between its segments); with that hole mapped it runs, but GLES is slower than on
  # g13p0 (docs/HARDWARE.md).
  if [ "${whichmali_bsp}" == "true" ] && [ "$FOLDER" == "armhf" ]; then
    MALI_BLOB=libmali-bifrost-g52-g13p0-gbm.so
  else
    MALI_BLOB=${whichmali}
  fi

  # Install Mali blob
  if [ "${whichmali_bsp}" == "true" ] && [ "$FOLDER" == "aarch64" ]; then
    # RK3562 64-bit: g29p1 (GLES + EGL + gbm + CL + Vulkan ICD). BSP/mali.tar.gz has g29p1-4;
    # the image ships g29p1-5 (rk_so_ver 5) from Rockchip's libmali mirror, pinned by commit
    # and checksum. A driver other than the one meant must not end up in an image, so a
    # failed download stops the build.
    MALI_URL="https://raw.githubusercontent.com/JeffyCN/mirrors/3e59dab3b69ab4e1dc2b0e6271a4174c108b9335/lib/aarch64-linux-gnu/${whichmali}"
    MALI_SHA256="605e3a5caf1be62bb48d97b1168434257355d56db5e3be9b515cfa742c173118"
    MALI_CACHE=Arkbuild_package_cache/${CHIPSET}/${whichmali%.so}-5.so
    echo "Installing Mali g29p1-5 (${whichmali})..."
    sudo mkdir -p Arkbuild_package_cache/${CHIPSET}
    if [ "$(sha256sum ${MALI_CACHE} 2>/dev/null | cut -c1-64)" != "${MALI_SHA256}" ]; then
      sudo wget -t 3 -T 60 -O ${MALI_CACHE}.part "${MALI_URL}" && sudo mv ${MALI_CACHE}.part ${MALI_CACHE}
    fi
    if [ "$(sha256sum ${MALI_CACHE} 2>/dev/null | cut -c1-64)" != "${MALI_SHA256}" ]; then
      echo "ERROR: ${whichmali} g29p1-5 did not download or has another checksum"
      exit 1
    fi
    sudo cp ${MALI_CACHE} Arkbuild/usr/lib/${ARCHITECTURE}/${whichmali}
    (
      cd Arkbuild/usr/lib/${ARCHITECTURE}
      sudo ln -sf ${whichmali} libMali.so
    )
  elif [ -f "Arkbuild/usr/lib/${ARCHITECTURE}/libmali.so.1" ]; then
    # Check if Mali already installed (e.g. from build_kernel step)
    # Resolve libmali.so.1 to the actual blob so libMali.so points directly to it,
    # avoiding a cycle when the symlink loop below sets libmali.so.1 -> libMali.so.
    echo "Mali libraries already installed, creating symlinks..."
    (
      cd Arkbuild/usr/lib/${ARCHITECTURE}
      MALI_REAL=$(readlink -f libmali.so.1)
      sudo ln -sf $(basename ${MALI_REAL}) libMali.so
    )
  else
    # Download Mali from core_builds repo (rk3566, rk3326, etc.).  Force the
    # output filename with -O — without it, wget appends .1 if a stale file
    # is present and the subsequent mv silently fails, leaving libEGL.so
    # dangling and breaking the libgo2 + EmulationStation build downstream.
    wget -t 3 -T 60 --no-check-certificate -O ${MALI_BLOB} https://github.com/christianhaitian/${CORE_BUILDS_CHIPSET}_core_builds/raw/refs/heads/master/mali/${FOLDER}/${MALI_BLOB}
    sudo mv ${MALI_BLOB} Arkbuild/usr/lib/${ARCHITECTURE}/.
    (
      cd Arkbuild/usr/lib/${ARCHITECTURE}
      sudo ln -sf ${MALI_BLOB} libMali.so
    )
  fi

  # Create EGL/GLES/GBM symlinks - use subshell to preserve cwd
  (
    cd Arkbuild/usr/lib/${ARCHITECTURE}
    for LIB in libEGL.so libEGL.so.1 libEGL.so.1.1.0 libGLES_CM.so libGLES_CM.so.1 libGLESv1_CM.so libGLESv1_CM.so.1 libGLESv1_CM.so.1.1.0 libGLESv2.so libGLESv2.so.2 libGLESv2.so.2.0.0 libGLESv2.so.2.1.0 libGLESv3.so libGLESv3.so.3 libgbm.so libgbm.so.1 libgbm.so.1.0.0 libmali.so libmali.so.1 libMaliOpenCL.so libOpenCL.so libwayland-egl.so libwayland-egl.so.1 libwayland-egl.so.1.0.0
    do
      sudo rm -fv ${LIB}
      sudo ln -sfv libMali.so ${LIB}
    done
  )
done
sudo chroot Arkbuild/ ldconfig -X

# EmulationStation uses the system Mali (g29p1) like every other app — g29p1's
# GLES 1.0 glDrawArrays works, so the old g24p0+g13p0 dual-blob hack (a separate
# g13p0 at /opt/emulationstation/lib reached via the ES binary's DT_RPATH) is gone.

# Install proprietary Rockchip Vulkan loader (rk3562 only)
# No system vulkan-headers needed — PPSSPP bundles its own in ext/vulkan/.
# The proprietary loader must be installed before PPSSPP builds so cmake can find it.
if [ "$CHIPSET" == "rk3562" ]; then
  sudo cp BSP/vulkan/libvulkan.so.1.3.274 Arkbuild/usr/lib/aarch64-linux-gnu/
  (
    cd Arkbuild/usr/lib/aarch64-linux-gnu
    sudo ln -sf libvulkan.so.1.3.274 libvulkan.so.1
    sudo ln -sf libvulkan.so.1 libvulkan.so
  )
fi

# Install meson
sudo chroot ${CHROOT_DIR}/ bash -c "git clone https://github.com/mesonbuild/meson.git && ln -s /meson/meson.py /usr/bin/meson"

# Install librga
if [ "$CHIPSET" == "rk3562" ]; then
  # RK3562 uses BSP librga (matches RGA3 kernel driver ABI) for both 32 and 64-bit.
  # The christianhaitian/linux-rga build has struct layout mismatches with the RGA3
  # kernel framework, causing "Cannot get src1 channel buffer" errors.
  if [ "$BIT" == "64" ]; then
    sudo cp BSP/librga/librga.so.2.1.0 ${CHROOT_DIR}/usr/lib/${ARCH}/
  else
    sudo cp BSP/librga32/librga.so.2.1.0 ${CHROOT_DIR}/usr/lib/${ARCH}/
  fi
  sudo ln -sf librga.so.2.1.0 ${CHROOT_DIR}/usr/lib/${ARCH}/librga.so.2
  sudo ln -sf librga.so.2 ${CHROOT_DIR}/usr/lib/${ARCH}/librga.so
  sudo mkdir -p ${CHROOT_DIR}/usr/local/include/rga
  sudo cp BSP/librga/include/RgaApi.h BSP/librga/include/drmrga.h BSP/librga/include/rga.h \
         BSP/librga/include/im2d.h BSP/librga/include/im2d_type.h BSP/librga/include/im2d_version.h \
         BSP/librga/include/im2d_common.h BSP/librga/include/im2d_buffer.h \
         BSP/librga/include/im2d_expand.h BSP/librga/include/im2d_single.h \
         BSP/librga/include/RgaUtils.h BSP/librga/include/RockchipRga.h \
         BSP/librga/include/RgaMutex.h BSP/librga/include/RgaSingleton.h \
         BSP/librga/include/GrallocOps.h BSP/librga/include/RockchipRgaMacro.h \
         ${CHROOT_DIR}/usr/local/include/rga/
else
  # Build and install christianhaitian's librga (works for RK3326/RK3566)
  sudo chroot ${CHROOT_DIR}/ bash -c "cd /home/ark &&
    git clone https://github.com/christianhaitian/linux-rga.git &&
    cd linux-rga &&
    meson build && cd build &&
    meson compile &&
    cp -r librga.so* /usr/lib/${ARCH}/ &&
    cd .. &&
    mkdir -p /usr/local/include/rga &&
    cp -f drmrga.h rga.h RgaApi.h RockchipRgaMacro.h /usr/local/include/rga/
    "
fi

# Build and install libgo2
sudo chroot ${CHROOT_DIR}/ bash -c "cd /home/ark &&
  git clone https://github.com/OtherCrashOverride/libgo2.git &&
  cd libgo2 &&
  premake4 gmake &&
  make -j$(nproc) &&
  cp libgo2.so* /usr/lib/${ARCH}/ &&
  mkdir -p /usr/include/go2 &&
  cp -L src/*.h /usr/include/go2/
  "

if [ "$BIT" == "64" ]; then
  save_rootfs_cache deps
fi
