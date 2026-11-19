#!/bin/bash
#
# Fix Audio for RK3562 (RG52 Mini)
# Resets audio configuration for RK817 codec, then restarts EmulationStation
#
# ES runs this from Options and waits for it with its own sound closed
# (GuiTools::launchTool). ES is restarted at the end, through the restart mark
# of its wrapper (emulationstation.sh): up to 1.11 this script killed ES first,
# the wrapper then exited with a failure, systemd stopped the whole service to
# restart it and this script went with it, often right after it had copied
# .asoundrcbak: with HDMI in, the sound was left on the speaker.
#

# Kill any audio-using processes
sudo killall -9 retroarch 2>/dev/null

# Restore default audio config
cp /home/ark/.asoundrcbak /home/ark/.asoundrc

# Restore ALSA state
sudo /usr/sbin/alsactl restore -f /var/local/asound.state 2>/dev/null

# Set playback path to speaker. On the RG52 Mini that is SPK: the codec drives
# the speaker amplifier (spk-ctl-gpios) only on that path, and HP silences the
# speaker; with headphones in, headphone-path.sh puts HP back. With HDMI in, it
# also moves the default sound device to HDMI (audio-switch.sh). The RG56Pro
# and RG43H run their speaker from the HP output, as spktoggle.sh has it.
if [ "$(cat /home/ark/.config/.DEVICE 2>/dev/null)" == "RG52MINI" ]; then
  amixer -q -c 0 sset 'Playback Path' SPK 2>/dev/null
  [ -x /usr/local/bin/headphone-path.sh ] && /usr/local/bin/headphone-path.sh
else
  amixer -q sset 'Playback Path' HP 2>/dev/null
fi

echo "Audio configuration reset for RK817 codec"
sleep 2

# Restart ES, the service staying up: the wrapper sees the mark and starts ES
# again, and that ES opens the sound device set above.
if killall -0 emulationstation 2>/dev/null; then
  touch /tmp/es-restart
  sudo killall -9 emulationstation 2>/dev/null
fi
