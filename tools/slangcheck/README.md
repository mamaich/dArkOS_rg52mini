# slangcheck: RetroArch's slang shaders, checked before a release

The image takes libretro's slang shaders from upstream's head at each build
(`build_retroarch.sh`; the owner, 2026-11-28: the new ones, but checked, since
1.13's set crashed RetroArch 1.22.2 on presets sized from the source until
patch 0027: docs/KNOWN-ISSUES.md, "RetroArch's shaders on the Mali-G52").
Loading all 2662 presets on the console would take hours (a Mega Bezel preset
takes 47 s to load on the Cortex-A53, crt-royale 21 s), so a release loads
only those the new set changes, with both sets.

1. In WSL, the previous release's image and the new one mounted:

       tools/slangcheck/slangdeps.py OLD/home/ark/.config/retroarch/shaders/shaders_slang \
           NEW/home/ark/.config/retroarch/shaders/shaders_slang -o changed.txt

   A preset is listed when any file RetroArch reads to load it differs: the
   `.slangp`, the presets it names with `#reference`, every pass's `.slang`
   with its `#include`s, its textures; and every preset the old set lacks.
   Presets the new set lacks are counted (a user's setting naming one loses it).
   The build's log names the commit the image took (also in
   `Arkbuild_package_cache/rk3562/slang-shaders.commit`).

2. Both sets to the console (`/home/ark/comptest/slang-old`, `slang-new`),
   with `changed.txt` and `slangcheck.py`; with ES's menu in front:

       sudo python3 slangcheck.py --root /home/ark/comptest/slang-old/shaders_slang \
           --list changed.txt --out old.tsv
       sudo python3 slangcheck.py --root /home/ark/comptest/slang-new/shaders_slang \
           --list changed.txt --out new.tsv

   It stops ES (and starts it again at the end, also on Ctrl+C or a dropped
   ssh), turns the volume to 0 and back, and runs the 64-bit RetroArch on its
   Vulkan driver (`--vulkan malisx2` for the other one) from a copy of ark's
   config with Genesis Plus GX and the 240p Test Suite. Each preset: `ok`,
   `fail` (RetroArch refused it; its error lines), `crash` or `hang` (no
   answer within `--timeout`, 300 s), with the seconds it took.

3. Compare the two: a preset that is `ok` with the old set and not with the
   new one holds the release. Pin `SLANG_COMMIT` in `build_retroarch.sh` at
   the old commit (or find out why), and build again.

Tried on 2026-11-28 on the 1.13 image's set (upstream 02bf134): crt-geom,
xbr-lv3, scalefx, xbrz-freescale, crt-royale and a Mega Bezel preset `ok`; a
preset naming a missing file and one with a syntax error `fail` with
RetroArch's message; a RetroArch killed while loading (`crash`) and one past
the timeout (`hang`) started again for the next preset.
