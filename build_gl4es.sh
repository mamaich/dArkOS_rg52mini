#!/bin/bash

# Build and install gl4es (OpenGL 2.1 over GLES2) at /usr/lib/gl4es, for PortMaster ports that
# bring none of their own: libgl_dArkOS.txt (portmaster-e2big.sh) points them there when the port
# has no gl4es folder, and a user can delete a port's gl4es to take this one. It carries our fix of
# gl4es's shader preprocessor (gl4es-fix/gl4es-preproc-conditional-defines.patch: a #define under
# an #if it cannot evaluate is no longer substituted; Don't Starve's blur and bloom shaders failed
# with "Array subscript too big"). Built as PortMaster's are: no X11, GLX stubs, its EGL wrapper,
# GBM for LIBGL_FB=4. See docs/KNOWN-ISSUES.md.
GL4ES_COMMIT=bc4314d8369f169d3fa81c5ed1b46b3dba4e74a5
sudo rm -rf Arkbuild/tmp/gl4es
call_chroot "cd /tmp && git clone https://github.com/ptitSeb/gl4es.git gl4es && cd gl4es && git checkout -q ${GL4ES_COMMIT}"
sudo cp gl4es-fix/gl4es-preproc-conditional-defines.patch Arkbuild/tmp/gl4es/
call_chroot "cd /tmp/gl4es && patch -Np1 < gl4es-preproc-conditional-defines.patch &&
  mkdir -p build && cd build &&
  cmake .. -DNOX11=ON -DGLX_STUBS=ON -DEGL_WRAPPER=ON -DGBM=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo &&
  make -j\20"
verify_action
if [ -f Arkbuild/tmp/gl4es/lib/libGL.so.1 ] && [ -f Arkbuild/tmp/gl4es/lib/libEGL.so.1 ]; then
  sudo mkdir -p Arkbuild/usr/lib/gl4es
  for f in libGL.so.1 libEGL.so.1; do
    sudo install -m 644 Arkbuild/tmp/gl4es/lib/$f Arkbuild/usr/lib/gl4es/
    call_chroot "strip --strip-unneeded /usr/lib/gl4es/$f"
  done
else
  # A failed patch or build stops the image build, as the other recipes do,
  # rather than shipping an image whose ports find no /usr/lib/gl4es.
  echo "gl4es did not build: no libGL.so.1 or libEGL.so.1. Stopping here."
  exit 1
fi
sudo rm -rf Arkbuild/tmp/gl4es
