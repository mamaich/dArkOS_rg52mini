#!/bin/sh
# build.sh: libnpusr_gles.so for the 32-bit RetroArch (armhf), run in the armhf build chroot (build_npusr.sh).
# rknn_api.h is vk-rotate-layer's, copied beside; the EGL and GLES headers are libegl-dev's and libgles-dev's
# (KHR/khrplatform.h libgl-dev's), unpacked into sys/ rather than installed (installing them would put libglvnd's libEGL over the chroot's
# libmali links). EGL_NO_X11: Debian's eglplatform.h wants X11 otherwise; nothing here uses it.
# Nothing but libc, libdl, libpthread and libm is linked: EGL, GLES and librknnrt are found at run time.
set -e
cd "$(dirname "$0")"
${CC:-gcc} -O2 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -fPIC -shared -fvisibility=hidden \
    -mcpu=cortex-a53 -mfpu=neon-fp-armv8 -mfloat-abi=hard -DEGL_NO_X11 -I. ${SYSINC:+-I"$SYSINC"} \
    -o libnpusr_gles.so npusr_gles.c -ldl -lpthread -lm
echo "built: $(ls -l libnpusr_gles.so)"
