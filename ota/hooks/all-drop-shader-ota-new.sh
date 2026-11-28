#!/bin/bash
# In every package, after the release's own hooks (the all-*.sh run in name order, this one first):
# no .ota-new beside RetroArch's shaders.
# The image carries libretro's GLSL shaders and (from 1.13) its slang ones, both cloned from
# upstream's head at each build, so a release may change any of them. A user who fetched shaders
# with RetroArch's Online Updater has copies of their own there, and for each file of theirs that
# differs from both images the client keeps it and puts the image's beside it as <name>.ota-new
# (docs/OTA.md, "How the client applies it", step 4): from 1.12 to 1.13 that could be thousands of
# files, which RetroArch never reads (it lists presets by their extension). The user's copies
# stay; the image's beside them go, and so does one beside a shader the user deleted (the client
# puts the new file beside a missing one too). The owner, 2026-11-28: remove them, from 1.14.
#
# Not in the merged list: a rollback has no use for them (the ones this update wrote are new
# files, which a rollback removes anyway). Never fails the update; safe to run twice.
PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
for d in /home/ark/.config/retroarch/shaders /home/ark/.config/retroarch32/shaders; do
  [ -d "$d" ] && [ ! -L "$d" ] || continue
  n=$(find "$d" -xdev \( -type f -o -type l \) -name '*.ota-new' -print -delete 2>/dev/null | wc -l)
  [ "$n" -gt 0 ] && echo "  $d: $n shaders' .ota-new removed (the user's own copies kept)"
done
exit 0
