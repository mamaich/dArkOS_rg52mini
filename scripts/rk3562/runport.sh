#!/bin/bash
# runport.sh <script>: start a port the way its author expects.
#
# PortMaster ports source PortMaster's control.txt. On ArkOS and dArkOS they
# are written for, and tested as, the ark user: control.txt finds sudo and
# routes what needs root through $ESUDO, and their saves live under
# /home/ark. They run as ark, as before. So does PortMaster.sh itself, which
# since PortMaster 2026.11 is a stub that sources utils/pm_launch.txt and
# names no control.txt: as root, get_controls looks for the pad's mapping in
# /root/.config/emulationstation, finds none, and A/B (and X/Y) come out
# swapped in PortMaster's own menus.
#
# Anything else in ports - scripts written for Knulli, ROCKNIX, muOS and the
# like, which run everything as root (the native Halo CE port is one) - runs
# as root, so that it works as unpacked, without editing the script.

port="$1"

# On a dual-boot card the ports live on Android's ext4, which has no mount
# option to make everything executable the way a FAT/exFAT umask does, and
# whatever arrives there through Android has no execute bit. A port script in
# /roms/ports without it marks a port that came in since: its folders - the
# subfolders of /roms/ports the script names in a path (".../ports/mina",
# "${PORTS_DIR}/doom3", "$(dirname "$0")/halo") - get u+x,g+x on every file
# that lacks it, all of /roms/ports below the top level if the script names
# none, and the script itself last, so that a pass cut short is done again on
# the next start. Ports already set up cost one look at the top-level scripts.
# As root: files Android wrote are not ark's. u+x,g+x keeps the group bits
# Android needs.
fix_new_ports() {
	local sh d name esc found
	for sh in /roms/ports/*.sh /roms/ports/*.SH; do
		[ -f "$sh" ] && [ ! -x "$sh" ] || continue
		found=0
		for d in /roms/ports/*/; do
			[ -d "$d" ] || continue
			name=$(basename "$d")
			esc=$(printf '%s' "$name" | sed 's/[][\\.*^$|+?(){}]/\\&/g')
			if grep -qE "/${esc}([\"'/ }]|\$)" "$sh"; then
				sudo find "$d" -type f ! -perm -u=x -exec chmod u+x,g+x {} + 2>/dev/null
				found=1
			fi
		done
		if [ "$found" = 0 ]; then
			sudo find /roms/ports -mindepth 2 -type f ! -perm -u=x -exec chmod u+x,g+x {} + 2>/dev/null
		fi
		sudo chmod u+x,g+x "$sh"
	done
}
if [ -f /boot/dualboot ] && [ -d /roms/ports ]; then
	fix_new_ports
fi

# A PortMaster port on a card that has no PortMaster yet - a fresh card, the
# port copied over by hand - starts without control.txt and mod_dArkOS.txt: no
# controls, no E2BIG guard, no gl4es-fix (Don't Starve: a black world). So the
# first one installs it, from the installer the image puts in Tools, which
# carries PortMaster.zip and needs no network. It would restart
# EmulationStation when done, under the port, unless ~/no_es_restart is there.
# portmaster-e2big adds our lines to the new mod_dArkOS.txt from its path unit;
# run here as well, they are in before the port reads it. The folders are the
# ones port scripts look in for control.txt.
install_portmaster() {
	local d inst
	for d in /opt/system/Tools /opt/tools "${XDG_DATA_HOME:-$HOME/.local/share}" /roms/ports; do
		[ -f "$d/PortMaster/control.txt" ] && return
	done
	for inst in /opt/system/Tools/Install.PortMaster.sh /roms/tools/Install.PortMaster.sh; do
		[ -f "$inst" ] || continue
		touch "$HOME/no_es_restart"
		bash "$inst"
		rm -f "$HOME/no_es_restart"
		sudo /usr/local/bin/portmaster-e2big.sh
		return
	done
}
if grep -q "control\.txt" "$port" 2>/dev/null; then
	install_portmaster
fi

# The GTA San Andreas port (folder gtasa) reads the pad through evdev itself
# and takes L2/R2's analog axes for the right stick: after a touch of a
# trigger, or its noise, the camera turned until the right stick was moved.
# trigaxes-off hides those two axes from it; L2/R2 still reach it as buttons
# (trigaxes-off/trigaxes-off.c).
trigaxes_off=/usr/lib/aarch64-linux-gnu/trigaxes-off/libtrigaxes-off.so
if [ -f "$trigaxes_off" ] && grep -qE "/gtasa([\"'/ }]|\$)" "$port" 2>/dev/null; then
	export LD_PRELOAD="$trigaxes_off${LD_PRELOAD:+:$LD_PRELOAD}"
fi

# A script without the execute bit still runs, through bash.

if grep -q -E "control\.txt|pm_launch\.txt" "$port" 2>/dev/null || [ "$(id -u)" -eq 0 ]; then
	[ -x "$port" ] && exec "$port"
	exec bash "$port"
fi
[ -x "$port" ] && exec sudo "$port"
exec sudo bash "$port"
