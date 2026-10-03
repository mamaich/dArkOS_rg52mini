#!/bin/sh
# Revision A workaround. On the RK915 revision of the RG52 Mini the panel stays
# black once the kernel takes over the display, and comes back after one
# suspend/resume cycle - which is what pressing the power button twice does,
# through ogage. This does the same once, by itself, after EmulationStation has
# started.
#
# A workaround, not a fix: no log from a revision A unit exists yet, so the cause
# is not known. Revision B is not affected and is left alone.

is_rev_a() {
    # The chip, whether or not its driver came up: RK915 answers as SDIO vendor
    # 0x0296, AIC8800 on revision B as 0xc8a1.
    grep -qx 0x0296 /sys/bus/sdio/devices/*/vendor 2>/dev/null && return 0
    lsmod | grep -q '^rk915 '
}

is_rev_a || exit 0

logger -t panel-kick "revision A: one suspend/resume cycle to bring the panel up"
sleep 8
# Same path ogage takes for the power button - systemd's suspend, with its sleep
# hooks - but with the RTC set to wake it. 5 s leaves room to get into suspend.
if rtcwake -m no -s 5 && systemctl suspend; then
    logger -t panel-kick "suspend requested, RTC wake in 5 s"
else
    logger -t panel-kick "could not suspend: rtcwake or systemctl failed"
fi
