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

if grep -q "control\.txt" "$port" 2>/dev/null || [ "$(id -u)" -eq 0 ]; then
	exec "$port"
fi
exec sudo "$port"
