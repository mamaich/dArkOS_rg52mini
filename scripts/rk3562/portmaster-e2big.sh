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
# PortMaster overwrites its files when it updates itself, so this runs at boot
# and from portmaster-e2big.path whenever mod_dArkOS.txt changes.

MARK="# dArkOS RG52 Mini: E2BIG guard"

for dir in /opt/system/Tools/PortMaster /roms/ports/PortMaster /roms2/ports/PortMaster /roms/tools/PortMaster; do
  f="$dir/mod_dArkOS.txt"
  [ -f "$f" ] || continue
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
