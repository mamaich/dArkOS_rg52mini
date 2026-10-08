#!/bin/bash
# runport.sh <script>: start a port the way its author expects.
#
# PortMaster ports source PortMaster's control.txt. On ArkOS and dArkOS they
# are written for, and tested as, the ark user: control.txt finds sudo and
# routes what needs root through $ESUDO, and their saves live under
# /home/ark. They run as ark, as before.
#
# Anything else in ports - scripts written for Knulli, ROCKNIX, muOS and the
# like, which run everything as root (the native Halo CE port is one) - runs
# as root, so that it works as unpacked, without editing the script.

port="$1"

# On a dual-boot card the ports live on Android's ext4, and whatever arrives
# there through Android or over the network has no execute bit; the boot-time
# pass of darkos-androidroms misses what was added since. Set it on what lacks
# it before every start (as root: Android's files are not ark's). u+x,g+x keeps
# the group bits Android needs.
if [ -f /boot/dualboot ] && [ -d /roms/ports ]; then
	sudo find /roms/ports -type f ! -perm -u=x -exec chmod u+x,g+x {} + 2>/dev/null
fi

# A script without the execute bit still runs, through bash.

if grep -q "control\.txt" "$port" 2>/dev/null || [ "$(id -u)" -eq 0 ]; then
	[ -x "$port" ] && exec "$port"
	exec bash "$port"
fi
[ -x "$port" ] && exec sudo "$port"
exec sudo bash "$port"
