#!/bin/bash
# In every package, after the release's own hooks (the all-*.sh run in name order, this one first):
# what updates leave beside RetroArch's shaders.
#
# The .ota-new files. The image carries libretro's GLSL shaders and (from 1.13) its slang ones,
# both cloned from upstream's head at each build, so a release may change any of them. A user who
# fetched shaders with RetroArch's Online Updater has copies of their own there, and for each file
# of theirs that differs from both images the client keeps it and puts the image's beside it as
# <name>.ota-new (docs/OTA.md, "How the client applies it", step 4): from 1.12 to 1.13 that could
# be thousands of files, which RetroArch never reads (it lists presets by their extension). The
# user's copies stay; the image's beside them go, and so does one beside a shader the user deleted
# (the client puts the new file beside a missing one too).
#
# The GLSL clones' git metadata. Images up to 1.12 kept shaders_glsl/.git of both RetroArchs (24 MB
# each; 1.13 leaves them out) and, inside, the Sharp-Shimmerless clone's (up to 1.13), and an update
# never removes them, since make-ota does not look into a .git. Nothing reads them. Only those, and
# only when their origin is still the clone the image made.
#
# The owner, 2026-11-28: remove both, from 1.14. Not in the merged list: a rollback has no use for
# them (the .ota-new files this update wrote are new files, which a rollback removes anyway).
# Never fails the update; safe to run twice.
PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
for r in /home/ark/.config/retroarch /home/ark/.config/retroarch32; do
  d=$r/shaders
  [ -d "$d" ] && [ ! -L "$d" ] || continue
  n=$(find "$d" -xdev \( -type f -o -type l \) -name '*.ota-new' -print -delete 2>/dev/null | wc -l)
  [ "$n" -gt 0 ] && echo "  $d: $n shaders' .ota-new removed (the user's own copies kept)"
  for g in "$d/shaders_glsl/.git libretro/glsl-shaders" \
           "$d/shaders_glsl/Sharp-Shimmerless/.git Woohyun-Kang/Sharp-Shimmerless-Shader"; do
    set -- $g
    [ -d "$1" ] && [ ! -L "$1" ] || continue
    grep -q -E "^[[:space:]]*url = https://github\.com/$2(\.git)?/?[[:space:]]*$" "$1/config" 2>/dev/null || continue
    k=$(du -sk "$1" 2>/dev/null | cut -f1)
    rm -rf "$1" && echo "  $1 removed (${k:-?} KB, the image's clone)"
  done
done
exit 0
