#!/bin/bash
# Switch ~/.asoundrc to HDMI when an HDMI display is connected, restore the
# default config when it's disconnected. Triggered by 99-hdmi-audio.rules on
# any DRM card add/change event.
#
# Only switch to HDMI when its sound card exists. On a kernel without the
# RK628 HDMI audio card (sai1 not probing) the default ALSA device would point
# at nothing and there would be no sound at all. The card id is cut to 15
# characters by ALSA (rockchiphdmirk6), so match on the card name instead.

HDMI_STATUS=$(cat /sys/class/drm/card0-HDMI-A-1/status 2>/dev/null)

if [ "$HDMI_STATUS" = "connected" ] && grep -q "hdmi-rk628" /proc/asound/cards 2>/dev/null; then
    cp /home/ark/.asoundrchdmi /home/ark/.asoundrc
else
    cp /home/ark/.asoundrcbak /home/ark/.asoundrc
fi
