#!/bin/sh
# Revision A workaround. On the RK915 revision of the RG52 Mini the panel stays
# black, backlight on, once the kernel has the display; a suspend/resume cycle
# brings it back. What the cycle actually does for it was found on a revision B
# unit, where the same black screen can be produced on purpose:
#
#   blank fb0, unblank 38 ms later   -> black, backlight on
#   same, panel supply held off 0.5s -> still black
#   blank fb0 for 3 s, then unblank  -> works
#
# fbcon restores the mode 38 ms after a blank, too soon for the DSI host, its
# PHY and the VOP to runtime-suspend. Re-enabled before they have powered down,
# the DSI link is left stale and the panel ignores its init sequence. Once they
# have suspended and come back from scratch - which suspend/resume also does -
# the panel initialises normally. The panel's own supply is not the issue.
#
# So: take fbcon off fb0 so nothing switches it back early, blank, wait until
# the DSI host has really suspended, unblank, give fbcon back. Only the display
# is touched, and it has to happen before EmulationStation: once a DRM master
# holds the device the blank is refused with -EBUSY.
#
# A workaround, not a fix: why revision A goes through a fast off/on at boot in
# the first place is still unknown.

is_rev_a() {
    # Revision A has no Type-C controller. Revision B's HUSB311 is built in and
    # bound long before userspace, so this needs no waiting.
    ! ls /sys/bus/i2c/drivers/husb311/ 2>/dev/null | grep -q -- '-004e$'
}

# /boot/panel-kick-force, an empty file on the FAT partition, runs it on any
# revision - so it can be tried on a revision B unit.
if [ -e /boot/panel-kick-force ]; then
    logger -t panel-kick "forced by /boot/panel-kick-force"
elif ! is_rev_a; then
    exit 0
fi

FB=/sys/class/graphics/fb0/blank
DSI=/sys/devices/platform/ffb10000.dsi/power/runtime_status
VT=
for v in /sys/class/vtconsole/vtcon*; do
    grep -q "frame buffer" "$v/name" 2>/dev/null && VT=$v/bind
done

logger -t panel-kick "re-initialising the panel before EmulationStation"
[ -n "$VT" ] && echo 0 > "$VT"
echo 4 > "$FB"
i=0
while [ "$(cat "$DSI" 2>/dev/null)" != suspended ] && [ $i -lt 30 ]; do
    sleep 0.1; i=$((i + 1))
done
logger -t panel-kick "DSI host $(cat "$DSI" 2>/dev/null) after $((i * 100)) ms"
sleep 0.2
echo 0 > "$FB"
[ -n "$VT" ] && echo 1 > "$VT"
logger -t panel-kick "panel re-initialised"
