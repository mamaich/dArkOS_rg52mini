#!/bin/sh
# build.sh: build libVkLayer_DARKOS_rotate.so (run on aarch64, or in an
# aarch64 chroot) - glslangValidator and the Vulkan headers needed.
set -e
cd "$(dirname "$0")"
glslangValidator -V --vn rotate_vert_spv -o rotate_vert.h rotate.vert > /dev/null
glslangValidator -V --vn rotate_frag_spv -o rotate_frag.h rotate.frag > /dev/null
cat rotate_vert.h rotate_frag.h > rotate_spv.h
# NPU UPSCALE (npusr.inc.c): its compute pass and the rotate pass that builds the enlarged frame
glslangValidator -V --target-env vulkan1.1 --vn sr_prep_spv -o sr_prep.h sr_prep.comp > /dev/null
glslangValidator -V --target-env vulkan1.1 --vn sr_rotate_frag_spv -o sr_rotate.h sr_rotate.frag > /dev/null
cat sr_prep.h sr_rotate.h > sr_spv.h
# rknn_api.h is beside it (rknn-toolkit2 v2.5.0, LICENSE.rknn); librknnrt.so is dlopen'ed only with SR on
${CC:-gcc} -O2 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -fPIC -shared \
    -fvisibility=hidden -I. -o libVkLayer_DARKOS_rotate.so darkos_rotate_layer.c -lpthread -ldl
rm -f rotate_vert.h rotate_frag.h sr_prep.h sr_rotate.h
echo "built: $(ls -l libVkLayer_DARKOS_rotate.so)"
