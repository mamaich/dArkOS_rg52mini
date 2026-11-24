#!/bin/sh
# build.sh: libnpusr_gles.so, run in the image's build chroots by build_npusr.sh: the armhf one (Arkbuild32) for the
# 32-bit RetroArch, the aarch64 one (Arkbuild) for the 64-bit one. rknn_api.h is vk-rotate-layer's, copied beside.
# EGL and GLES headers: the aarch64 chroot has libegl-dev, libgles-dev and libgl-dev installed (the deps); for armhf
# they are unpacked into sys/ rather than installed (installing them would put libglvnd's libEGL over the chroot's
# libmali links) and given as SYSINC. EGL_NO_X11: Debian's eglplatform.h wants X11 otherwise; nothing here uses it.
# Nothing but libc, libdl, libpthread and libm is linked: EGL, GLES and librknnrt are found at run time.
set -e
cd "$(dirname "$0")"
case "$(${CC:-gcc} -dumpmachine)" in
  aarch64*) ARCH="-mcpu=cortex-a53" ;;
  *) ARCH="-mcpu=cortex-a53 -mfpu=neon-fp-armv8 -mfloat-abi=hard" ;;
esac
${CC:-gcc} -O2 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -fPIC -shared -fvisibility=hidden \
    $ARCH -DEGL_NO_X11 -I. ${SYSINC:+-I"$SYSINC"} \
    -o libnpusr_gles.so npusr_gles.c -ldl -lpthread -lm
echo "built: $(ls -l libnpusr_gles.so)"
