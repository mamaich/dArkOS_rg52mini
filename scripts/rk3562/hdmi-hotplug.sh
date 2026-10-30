#!/bin/bash
# Re-route EmulationStation between the internal panel and HDMI on hotplug.
# hdmi-test.sh decides this only once at boot, so a cable plugged in later did
# nothing and unplugging left the console pointing at a dead HDMI output.
# Triggered by 99-hdmi-hotplug.rules on every DRM connector change.
#
# A cable that is wiggled or half in sends a burst of changes, so the events are
# coalesced (one handler runs, at most one more waits and reads the cable afresh
# when it gets its turn) and a state is acted on only when it holds for a second.
exec 8>/run/hdmi-hotplug.queue
flock -n 8 || exit 0             # one is already waiting: it will see this change
exec 9>/run/hdmi-hotplug.lock
flock -w 60 9 || exit 0
exec 8>&-                        # running now: the next event may queue

hdmi() { [ "$(cat /sys/class/drm/card0-HDMI-A-1/status 2>/dev/null)" = connected ] && echo 1 || echo 0; }
sleep 2                          # let the bridge/EDID settle
want=$(hdmi)
cur=$(cat /var/run/drmConn 2>/dev/null || echo 0)
[ "$want" = "$cur" ] && exit 0
# Debounce: the new state has to hold for a second, read five times.
for i in 1 2 3 4 5; do
  sleep 0.2
  [ "$(hdmi)" = "$want" ] || exit 0
done

# EmulationStation stays alive while a game runs: the game is its child, and a
# restart ends it with its unsaved progress.
# The games started from Ports are not its children: the launchers run them in a
# systemd unit of their own (NAME-port-UID, from systemd-run). A restart of
# EmulationStation does not end those; they would go on drawing to the output that
# just went away and keep the display lock, and the next launch would close at
# once. They count as a game here, and are stopped with it.
es=$(pgrep -f '^/usr/bin/emulationstation/emulationstation' | head -1)
ports() { systemctl list-units --type=service --state=running --plain --no-legend '*-port-*.service' 2>/dev/null | awk '{print $1}'; }
busy() { { [ -n "$es" ] && pgrep -P "$es" > /dev/null; } || [ -n "$(ports)" ]; }
if busy; then
  if [ "$want" = 1 ]; then
    # Plugged in during a game, which goes on on the panel: switch when it ends.
    # One waiter at a time; it lets the lock go and runs this script again, which
    # reads the cable afresh.
    exec 9>&-
    exec 7>/run/hdmi-hotplug.wait
    flock -n 7 || exit 0
    logger -t hdmi-hotplug "HDMI connected during a game: switching when it ends"
    while busy; do sleep 2; done
    exec 7>&-
    exec "$0"
  fi
  # Unplugged during a game on HDMI: its picture is gone, and a game drawing on
  # HDMI cannot move to the panel, so it is ended with EmulationStation.
  logger -t hdmi-hotplug "HDMI disconnected during a game: the game is ended"
  # Write the choice first: a Port started without the frontend starts it again
  # when it ends, and it has to read the new state.
  echo "$want" > /var/run/drmConn
  units=$(ports)
  if [ -n "$units" ]; then
    logger -t hdmi-hotplug "stopping the Ports games: $(echo $units)"
    timeout 20 systemctl stop $units
  fi
fi

echo "$want" > /var/run/drmConn
logger -t hdmi-hotplug "HDMI $([ $want = 1 ] && echo connected || echo disconnected): drmConn $cur -> $want"
if [ -n "$es" ]; then
  systemctl restart emulationstation
fi
# Back on the internal screen: switching the panel off for HDMI also left the
# backlight at 0, so restore the last brightness (kept in /run/darkos-brightness).
if [ "$want" = 0 ]; then
  sleep 3
  bl=/sys/class/backlight/backlight
  if [ "$(cat $bl/brightness 2>/dev/null || echo 0)" -lt 2 ]; then
    saved=$(cat /run/darkos-brightness 2>/dev/null || echo 60)
    [ "$saved" -ge 2 ] 2>/dev/null || saved=60
    echo "$saved" > $bl/brightness
    logger -t hdmi-hotplug "backlight was 0, restored to $saved"
  fi
fi
