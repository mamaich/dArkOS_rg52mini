#!/bin/bash
# RG52 Mini: the RK817's playback path follows the headphone jack, HP with headphones in and
# SPK without. The speaker's amplifier is fed from the headphone output, and the codec turns
# the amplifier on (spk-ctl-gpios) only on the SPK path: left on SPK, the sound came out of
# the headphones and the speaker at once. Android's audio HAL switches the path the same way
# on the jack's event; nothing here did.
#
# Run by 99-darkos-headphone.rules on every change of the jack's extcon (rk817-sound), by
# spktoggle.sh at boot after it restores the mixer state saved at shutdown (which may hold
# either path), and by Options > Advanced > Fix Audio.
# The RG56PRO and the RG43H run their speaker from the HP path (spktoggle.sh): not for them.
PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
[ "$(cat /home/ark/.config/.DEVICE 2>/dev/null)" = RG52MINI ] || exit 0

hp=0
for e in /sys/class/extcon/extcon*; do
  [ "$(cat "$e/name" 2>/dev/null)" = rk817-sound ] || continue
  grep -q '^HEADPHONE=1' "$e/state" 2>/dev/null && hp=1
done
want=SPK
[ $hp = 1 ] && want=HP

# card 0 is the RK817 (with HDMI in, ~/.asoundrc points the default at the HDMI card)
cur=$(amixer -c 0 sget 'Playback Path' 2>/dev/null | sed -n "s/.*Item0: '\(.*\)'.*/\1/p")
[ "$cur" = "$want" ] && exit 0
amixer -q -c 0 sset 'Playback Path' "$want" &&
  logger -t headphone-path "playback path ${cur:-?} -> $want"
exit 0
