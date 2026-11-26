#!/bin/bash
# PortMaster does not know the RG52 Mini's joystick (play_joystick), so its
# get_controls() puts the whole gamecontrollerdb.txt - about 470 KB - into
# sdl_controllerconfig, and every port exports that as SDL_GAMECONTROLLERCONFIG.
# One environment string may not exceed MAX_ARG_STRLEN (128 KB), so after the
# export every exec fails with "Argument list too long" and no port starts.
#
# PortMaster already guards against this on EmuELEC (mod_EmuELEC.txt); this
# appends the same guard to mod_dArkOS.txt, which every port sources after
# control.txt. SDL still gets the mappings, including the one mapper.py adds
# for this joystick, through SDL_GAMECONTROLLERCONFIG_FILE.
#
# Not every port sources mod_dArkOS.txt (GTA San Andreas sources control.txt,
# calls get_controls and exports SDL_GAMECONTROLLERCONFIG itself). Up to
# PortMaster 2026.06 that did no harm: for a pad it does not know, get_controls
# started the file empty and mapper.py added this joystick's line. 2026.11
# starts it from all of gamecontrollerdb.txt, so such a port failed with
# "Argument list too long" on every program it ran. So get_controls in
# control.txt gets the guard too, keeping mapper.py's lines for this pad.
#
# It also points gl4es at the render-target fix (LIBGL_GLES), and lets the Vulkan driver chosen in
# ES (DARKOS_VULKAN) through to a port run with $ESUDO, see below. And it keeps libgl_dArkOS.txt,
# which gl4es ports source instead of libgl_default.txt: the same, and the image's gl4es
# (/usr/lib/gl4es, build_gl4es.sh) for a port without a gl4es of its own.
#
# And it drops PortMaster's cache of what it found of the device (--drop-cache does only that).
# PortMaster 2026.11 probes the device with device_info.txt, the screen's size from the first
# connected DRM connector, and keeps the result in device_info_<cfw>_<device>.env until its
# own script's version changes. The console switches between the panel (1280x720) and HDMI
# (the TV's mode), so a cache made on one told PortMaster and the ports the other's size: its
# window drew 1920x1080 on the panel after one start on the TV. Dropped at boot and by
# hdmi-hotplug.sh at each switch, so the next start probes the output in use.
#
# And it gives PortMaster's window the whole screen in the stable channel's 2026.06, which images
# up to 1.8 and from 1.13 install: PortMaster has no entry for this device, so its GUI (pugwash)
# took the default 640x480, a 4:3 window with black bars on each side of the 16:9 screen. pugwash
# already asks SDL for the screen's size, but uses it only for a device it calls "default"; the
# patch below uses it for any device its table lacks. 2026.11 (the beta that 1.9 to 1.12
# installed) finds the size itself, and its pugwash is only a launcher, left alone. (1.11 and
# 1.12 had dropped this patch, as their image's PortMaster did not need it.)
#
# And get_controls hides the console's own controls from a port's SDL when a pad is in
# (darkos-portpads, ES's EMULATOR SETTINGS > HIDE BUILT-IN GAMEPAD of the Ports, es-0010):
# most ports take SDL's first game controller, and the console's controls come first. Not for
# PortMaster's own menu (pm_launch.txt calls get_controls as well), which reads every pad.
#
# PortMaster overwrites its files when it updates itself, so this runs at boot
# and from portmaster-e2big.path whenever mod_dArkOS.txt or control.txt changes.

MARK="# dArkOS RG52 Mini: E2BIG guard"
MARK_GL4ES="# dArkOS RG52 Mini: gl4es render-target fix"
MARK_VULKAN="# dArkOS RG52 Mini: Vulkan driver through sudo"
MARK_LIBGL="# dArkOS RG52 Mini: the image's gl4es"

# control.txt: get_controls keeps sdl_controllerconfig small. Prints "patched" when it did;
# exit 2 when the line it follows is not there, and then it leaves the file alone.
control_e2big() {
  python3 - "$1" <<'PY'
import sys
MARK = "# dArkOS RG52 Mini: E2BIG guard in get_controls"
OLD = '  sdl_controllerconfig="$(< "${SDL_GAMECONTROLLERCONFIG_FILE}")"\n'
NEW = OLD + '''  # dArkOS RG52 Mini: E2BIG guard in get_controls (added by /usr/local/bin/portmaster-e2big.sh)
  # For a pad PortMaster does not know, 2026.11 starts the file from all of gamecontrollerdb.txt;
  # a port that exports SDL_GAMECONTROLLERCONFIG itself then cannot run anything (E2BIG).
  # Only the lines mapper.py added for this pad, as up to 2026.06; SDL reads the whole file.
  if [ "${#sdl_controllerconfig}" -gt 100000 ]; then
    sdl_controllerconfig="$(sed -n '/^# Custom Entries/,$p' "${SDL_GAMECONTROLLERCONFIG_FILE}" | grep -v '^#')"
  fi
'''
p = sys.argv[1]
s = open(p, encoding="utf-8", newline="").read()
if MARK in s:
    sys.exit(0)
if s.count(OLD) != 1:
    sys.exit(2)
# in place: the file keeps its owner, mode and ACL
open(p, "w", encoding="utf-8", newline="").write(s.replace(OLD, NEW))
print("patched")
PY
}

# control.txt: get_controls hides the console's controls from SDL when darkos-portpads says so.
# Same anchor and exit codes as control_e2big.
control_hidepad() {
  python3 - "$1" <<'PY'
import sys
MARK = "# dArkOS RG52 Mini: built-in pad hidden"
OLD = '  sdl_controllerconfig="$(< "${SDL_GAMECONTROLLERCONFIG_FILE}")"\n'
NEW = OLD + '''  # dArkOS RG52 Mini: built-in pad hidden from the port's SDL (added by /usr/local/bin/portmaster-e2big.sh)
  # Most ports take SDL's first game controller, and the console's controls come first: with a
  # pad in, they are left out (SDL_GAMECONTROLLER_IGNORE_DEVICES), as ES's HIDE BUILT-IN GAMEPAD
  # of the Ports says (DARKOS_HIDE_PAD: auto when unset, yes, no). Not for PortMaster's own menu.
  # gptokeyb runs through sudo, which drops the variable: Select + Start still ends the port.
  # (PortMaster's menu calls get_controls from pm_launch.txt in 2026.11, from PortMaster.sh in 2026.06.)
  case "${BASH_SOURCE[*]}" in
    *pm_launch.txt*|*/PortMaster.sh*) ;;
    *)
      if [ -x /usr/local/bin/darkos-portpads ]; then
        _darkos_ignore="$(/usr/local/bin/darkos-portpads "${DARKOS_HIDE_PAD:-auto}")"
        if [ -n "$_darkos_ignore" ]; then
          export SDL_GAMECONTROLLER_IGNORE_DEVICES="$_darkos_ignore"
          echo "dArkOS: the console's controls hidden from the game (SDL_GAMECONTROLLER_IGNORE_DEVICES=$_darkos_ignore)"
        fi
        unset _darkos_ignore
      fi
      ;;
  esac
'''
p = sys.argv[1]
s = open(p, encoding="utf-8", newline="").read()
if MARK in s:
    sys.exit(0)
if s.count(OLD) != 1:
    sys.exit(2)
# in place: the file keeps its owner, mode and ACL
open(p, "w", encoding="utf-8", newline="").write(s.replace(OLD, NEW))
print("patched")
PY
}

# pugwash (PortMaster 2026.06, the stable channel): a device PortMaster's HW_INFO lacks gets the
# screen's own size, as "default" does. Prints what it did; exit 2 when the code it patches is not
# there in a pugwash that has it otherwise, exit 3 for a pugwash that is only a launcher (2026.11,
# which finds the size itself); either way it leaves the file alone.
pugwash_screen_size() {
  python3 - "$1" <<'PY'
import sys
MARK = "# dArkOS RG52 Mini: screen size"
OLD = """        if capabilities['device'] == 'default':
            capabilities = harbourmaster.device_info(override_resolution=(self.display_width, self.display_height))
"""
NEW = OLD + """        elif capabilities['device'] not in harbourmaster.HW_INFO:
            # dArkOS RG52 Mini: screen size (added by /usr/local/bin/portmaster-e2big.sh)
            # PortMaster has no entry for this device, so it got the default 640x480: a 4:3
            # window with black bars on the 16:9 screen. The screen's size, as for "default".
            capabilities = harbourmaster.device_info(override_resolution=(self.display_width, self.display_height))
"""
p = sys.argv[1]
s = open(p, encoding="utf-8", newline="").read()
if MARK in s:
    sys.exit(0)
if "override_resolution" not in s:
    sys.exit(3)
if s.count(OLD) != 1:
    sys.exit(2)
# in place: the file keeps its owner, mode and ACL
open(p, "w", encoding="utf-8", newline="").write(s.replace(OLD, NEW))
print("patched")
PY
}

DIRS="/opt/system/Tools/PortMaster /roms/ports/PortMaster /roms2/ports/PortMaster /roms/tools/PortMaster"

drop_cache() {
  local dir c
  for dir in $DIRS; do
    for c in "$dir"/device_info_*.env; do
      [ -f "$c" ] && rm -f "$c" && logger -t portmaster-e2big "dropped $c (the screen in use is probed again)"
    done
  done
}

if [ "$1" = --drop-cache ]; then
  drop_cache
  exit 0
fi
[ "$1" = --boot ] && drop_cache

for dir in $DIRS; do
  f="$dir/mod_dArkOS.txt"
  [ -f "$f" ] || continue
  if ! grep -qF "$MARK_GL4ES" "$f" && [ -f /usr/lib/aarch64-linux-gnu/gl4es-fix/libGLESv2.so.2 ]; then
    cat >> "$f" <<'GL4ES'

# dArkOS RG52 Mini: gl4es render-target fix (added by /usr/local/bin/portmaster-e2big.sh)
# gl4es leaves the textures a game renders into with the GLES default min filter
# GL_NEAREST_MIPMAP_LINEAR; without mipmaps libmali reads them as black (Don't
# Starve: a black world). This library forwards every GLES call to libmali and
# gives such textures GL_LINEAR. Only gl4es reads LIBGL_GLES; a port that sets
# its own keeps it.
if [ -z "$LIBGL_GLES" ] && [ -f /usr/lib/aarch64-linux-gnu/gl4es-fix/libGLESv2.so.2 ]; then
    export LIBGL_GLES=/usr/lib/aarch64-linux-gnu/gl4es-fix/libGLESv2.so.2
fi
GL4ES
    logger -t portmaster-e2big "gl4es fix added to $f"
  fi
  if ! grep -qF "$MARK_VULKAN" "$f"; then
    cat >> "$f" <<'VULKAN'

# dArkOS RG52 Mini: Vulkan driver through sudo (added by /usr/local/bin/portmaster-e2big.sh)
# ES's EMULATOR SETTINGS > VULKAN DRIVER for the ports reaches them as DARKOS_VULKAN, which
# libvulkan_darkos.so reads; $ESUDO (sudo --preserve-env=<list>) would drop it for a port that
# runs its game through sudo.
if [ -n "$DARKOS_VULKAN" ] && [[ "$ESUDO" == sudo* ]] && [[ "$ESUDO" != *DARKOS_VULKAN* ]]; then
    if [[ "$ESUDO" == *--preserve-env=* ]]; then
        ESUDO="${ESUDO/--preserve-env=/--preserve-env=DARKOS_VULKAN,}"
    else
        ESUDO="$ESUDO --preserve-env=DARKOS_VULKAN"
    fi
    export ESUDO
fi
VULKAN
    logger -t portmaster-e2big "Vulkan driver pass-through added to $f"
  fi
  # gl4es ports source libgl_${CFW_NAME}.txt, else libgl_default.txt; PortMaster has no dArkOS one
  lg="$dir/libgl_dArkOS.txt"
  if [ -f /usr/lib/gl4es/libGL.so.1 ] && ! grep -qF "$MARK_LIBGL" "$lg" 2>/dev/null; then
    [ -f "$lg" ] || cp "$dir/libgl_default.txt" "$lg" 2>/dev/null || : > "$lg"
    cat >> "$lg" <<'LIBGL'

# dArkOS RG52 Mini: the image's gl4es (added by /usr/local/bin/portmaster-e2big.sh)
# A port without a gl4es folder of its own (or one whose folder the user deleted) gets the
# image's, built with our fix of gl4es's shader preprocessor (#defines under an #if it cannot
# evaluate). Without it, libGL.so.1 would be Debian's, which needs X.
if [ ! -d "$PWD/gl4es.$DEVICE_ARCH" ] && [ ! -d "$PWD/gl4es" ] && [ -f /usr/lib/gl4es/libGL.so.1 ]; then
    export LD_LIBRARY_PATH="/usr/lib/gl4es:$LD_LIBRARY_PATH"
fi
LIBGL
    logger -t portmaster-e2big "image gl4es fallback added to $lg"
  fi
  ct="$dir/control.txt"
  if [ -f "$ct" ]; then
    r=$(control_e2big "$ct"); rc=$?
    [ "$r" = patched ] && logger -t portmaster-e2big "E2BIG guard added to get_controls in $ct"
    [ $rc = 2 ] && logger -t portmaster-e2big "$ct: get_controls not as expected, left as it is"
    r=$(control_hidepad "$ct"); rc=$?
    [ "$r" = patched ] && logger -t portmaster-e2big "built-in pad hiding added to get_controls in $ct"
    [ $rc = 2 ] && logger -t portmaster-e2big "$ct: get_controls not as expected, no built-in pad hiding"
  fi
  pw="$dir/pugwash"
  if [ -f "$pw" ]; then
    r=$(pugwash_screen_size "$pw"); rc=$?
    [ "$r" = patched ] && logger -t portmaster-e2big "screen size added to $pw"
    [ $rc = 2 ] && logger -t portmaster-e2big "$pw: code for the screen size not found, left as it is"
  fi
  grep -qF "$MARK" "$f" && continue
  cat >> "$f" <<'GUARD'

# dArkOS RG52 Mini: E2BIG guard (added by /usr/local/bin/portmaster-e2big.sh)
# get_controls reads the full gamecontrollerdb.txt into sdl_controllerconfig when
# no device GUID is known. Ports export it as SDL_GAMECONTROLLERCONFIG, which
# exceeds MAX_ARG_STRLEN (131072 bytes) and makes every execve() fail with E2BIG.
# SDL still gets the mappings via SDL_GAMECONTROLLERCONFIG_FILE.
if declare -f get_controls >/dev/null 2>&1; then
    eval "_pm_get_controls_orig() $(declare -f get_controls | tail -n +2)"

    get_controls() {
        _pm_get_controls_orig "$@"
        [ "${#sdl_controllerconfig}" -gt 100000 ] && sdl_controllerconfig=""
    }
fi
GUARD
  logger -t portmaster-e2big "guard added to $f"
done
exit 0
