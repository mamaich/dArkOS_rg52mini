#!/bin/bash
# Switch ~/.asoundrc to HDMI when an HDMI display is connected, restore the
# default config when it's disconnected. Triggered by 99-hdmi-audio.rules on
# any DRM card add/change event.
#
# Only switch to HDMI when its sound card exists. On a kernel without the
# RK628 HDMI audio card (sai1 not probing) the default ALSA device would point
# at nothing and there would be no sound at all. The card id is cut to 15
# characters by ALSA (rockchiphdmirk6), so match on the card name instead.
#
# Headphones in the RK817's jack win over HDMI: the sound stays on card 0, whose
# playback path headphone-path.sh has set to HP. headphone-path.sh runs this
# again on each plug, so pulling the headphones out moves the sound back to HDMI.
# An app picks the new device up when it opens the sound device next (a game
# started after the change; ES when it restarts).

HDMI_STATUS=$(cat /sys/class/drm/card0-HDMI-A-1/status 2>/dev/null)

HEADPHONES=0
for e in /sys/class/extcon/extcon*; do
    [ "$(cat "$e/name" 2>/dev/null)" = rk817-sound ] || continue
    grep -q '^HEADPHONE=1' "$e/state" 2>/dev/null && HEADPHONES=1
done

if [ "$HDMI_STATUS" = "connected" ] && [ "$HEADPHONES" = 0 ] &&
   grep -q "hdmi-rk628" /proc/asound/cards 2>/dev/null; then
    cp /home/ark/.asoundrchdmi /home/ark/.asoundrc
else
    cp /home/ark/.asoundrcbak /home/ark/.asoundrc
fi
