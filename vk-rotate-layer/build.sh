#!/bin/sh
# build.sh: build libVkLayer_DARKOS_rotate.so (run on aarch64, or in an
# aarch64 chroot) - glslangValidator and the Vulkan headers needed.
set -e
cd "$(dirname "$0")"
glslangValidator -V --vn rotate_vert_spv -o rotate_vert.h rotate.vert > /dev/null
glslangValidator -V --vn rotate_frag_spv -o rotate_frag.h rotate.frag > /dev/null
cat rotate_vert.h rotate_frag.h > rotate_spv.h
# NPU UPSCALE (npusr.inc.c): the native frame and the NPU's input, the passes that draw the enlarged frame
SR=""
for f in sr_prep.comp:sr_prep_spv sr_prep_cell.comp:sr_prep_cell_spv sr_nat.vert:sr_nat_vert_spv sr_nat.frag:sr_nat_frag_spv \
         sr_nin_s2d.comp:sr_nin_s2d_spv sr_rotate.frag:sr_rotate_frag_spv sr_decode.frag:sr_decode_frag_spv \
         sr_frame.frag:sr_frame_frag_spv sr_rect.frag:sr_rect_frag_spv sr_ring.frag:sr_ring_frag_spv; do
    glslangValidator -V --target-env vulkan1.1 --vn ${f#*:} -o ${f#*:}.h ${f%%:*} > /dev/null
    SR="$SR ${f#*:}.h"
done
cat $SR > sr_spv.h
# rknn_api.h is beside it (rknn-toolkit2 v2.5.0, LICENSE.rknn); librknnrt.so is dlopen'ed only with SR on
${CC:-gcc} -O2 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -fPIC -shared \
    -fvisibility=hidden -I. -o libVkLayer_DARKOS_rotate.so darkos_rotate_layer.c -lpthread -ldl
rm -f rotate_vert.h rotate_frag.h $SR
echo "built: $(ls -l libVkLayer_DARKOS_rotate.so)"
