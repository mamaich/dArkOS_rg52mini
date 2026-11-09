#!/bin/bash
# Update.sh (RG52 Mini): over-the-air updates of dArkOS with darkos-ota.
# Replaces the generic Update.sh, which fetches upstream dArkOS updates built
# for other chipsets. A package found in /roms/update is offered first, so
# that an update can also be copied to the card by hand.

sudo chmod 666 /dev/tty1
reset
printf "\e[?25l" > /dev/tty1
export TERM=linux
export XDG_RUNTIME_DIR=/run/user/$UID/
sudo setfont /usr/share/consolefonts/Lat7-TerminusBold22x11.psf.gz
height="15"
width="60"

ExitMenu() {
  printf "\033c" > /dev/tty1
  if [[ ! -z $(pgrep -f gptokeyb) ]]; then
    pgrep -f gptokeyb | sudo xargs kill -9
  fi
  sudo setfont /usr/share/consolefonts/Lat7-Terminus20x10.psf.gz
  exit 0
}

Msg() {
  dialog --clear --title "$1" --msgbox "\n$2" $height $width 2>&1 > /dev/tty1
}

LastError() {
  sudo tail -n 20 /var/lib/darkos-ota/ota.log 2>/dev/null | grep "ERROR\|FAILED" | tail -n 1 | sed 's/^[0-9: -]*//'
}

# Run darkos-ota with its output on screen; returns its exit status.
Run() {
  sudo /usr/local/sbin/darkos-ota "$@" 2>&1 | grep --line-buffered -v '^PROGRESS' | \
    dialog --clear --title "dArkOS update" --progressbox $height $width > /dev/tty1
  return ${PIPESTATUS[0]}
}

Apply() {
  # $1: what to show, rest: darkos-ota arguments
  local what="$1"; shift
  dialog --clear --title "dArkOS update" --yes-label "Update" --no-label "Cancel" \
    --yesno "\nInstall $what?\n\nThe update cannot be stopped once started. Keep the device on (or on the charger) until it says it is done." \
    $height $width 2>&1 > /dev/tty1 || return
  if Run "$@"; then
    Msg "dArkOS update" "$what is installed. The device will now restart."
    sudo reboot
    exit 0
  fi
  Msg "Update failed" "$(LastError)\n\nNothing was changed, or the change was undone. The log is /var/lib/darkos-ota/ota.log."
}

# Files in the update folders that cannot be installed here, said rather than passed over:
# a package cut short in the download (or an error page saved under its name) used to leave
# only "up to date" or a network error on the screen.
Problems() {
  local probs text="" path kind detail name build
  probs=$(sudo /usr/local/sbin/darkos-ota local-problems 2>/dev/null)
  [ -n "$probs" ] || return
  build=$(/usr/local/sbin/darkos-ota version | cut -d' ' -f2)
  while IFS=$'\t' read -r path kind detail; do
    name=$(basename "$path")
    case "$kind" in
      damaged) text+="$name: damaged or not downloaded completely ($detail). Download it again.\n\n" ;;
      not-tar) text+="$name: not a .tar file ($detail). Was its download finished?\n\n" ;;
      other-build) text+="$name: made for another build, from $detail. This console runs dArkOS $1 (build $build): take the package that starts from it.\n\n" ;;
    esac
  done <<< "$probs"
  # taller and wider than Msg: three files with their long names did not fit 15 x 60
  [ -n "$text" ] && dialog --clear --title "Update folder" --msgbox "\n$text" 22 72 2>&1 > /dev/tty1
}

Check() {
  local cur build pkgs entry
  cur=$(/usr/local/sbin/darkos-ota version | cut -d' ' -f1)
  build=$(/usr/local/sbin/darkos-ota version | cut -d' ' -f2)
  pkgs=$(sudo /usr/local/sbin/darkos-ota local 2>/dev/null)
  if [ -n "$pkgs" ]; then
    local file ver
    file=$(echo "$pkgs" | tail -n 1 | cut -f1)
    ver=$(echo "$pkgs" | tail -n 1 | cut -f2)
    Apply "dArkOS $ver from $(basename "$file")" install "$file"
    return
  fi
  Problems "$cur"
  dialog --infobox "\nChecking for updates. Please wait..." 5 $width > /dev/tty1
  sudo timedatectl set-ntp 1 2>/dev/null
  entry=$(sudo /usr/local/sbin/darkos-ota check 2>&1 | tail -n 1)
  case "$entry" in
    none)
      Msg "dArkOS update" "dArkOS $cur (build $build) is up to date." ;;
    \{*)
      local ver size
      ver=$(echo "$entry" | jq -r .version)
      size=$(( $(echo "$entry" | jq -r .size) / 1048576 ))
      Apply "dArkOS $ver (download $size MiB)" update ;;
    *)
      Msg "dArkOS update" "Could not check for updates. Is Wi-Fi connected?\n\n$(LastError)" ;;
  esac
}

Rollback() {
  dialog --clear --title "dArkOS update" --yes-label "Roll back" --no-label "Cancel" \
    --yesno "\nReturn to dArkOS $1, the version before the last update?" $height $width 2>&1 > /dev/tty1 || return
  if Run rollback; then
    Msg "dArkOS update" "Rolled back to dArkOS $1. The device will now restart."
    sudo reboot
    exit 0
  fi
  Msg "Roll back failed" "$(LastError)"
}

MainMenu() {
  while true; do
    local cur state from opts
    cur=$(/usr/local/sbin/darkos-ota version | cut -d' ' -f1)
    state=$(sudo /usr/local/sbin/darkos-ota status 2>/dev/null)
    from=$(echo "$state" | sed '/^snapshots:/d' | jq -r '.from_version // empty' 2>/dev/null)
    opts=( 1 "Check for updates" )
    if echo "$state" | grep -q '"status": "\(applied\|booted\)"' && \
       echo "$state" | grep -q '^snapshots: /'; then
      opts+=( 2 "Roll back to dArkOS $from" )
    fi
    opts+=( 3 "Exit" )
    choice=$(dialog --clear --backtitle "dArkOS $cur" --title "dArkOS update" \
      --no-collapse --cancel-label "Select + Start to Exit" \
      --menu "Please make your selection" $height $width 15 "${opts[@]}" 2>&1 > /dev/tty1) || ExitMenu
    case $choice in
      1) Check ;;
      2) Rollback "$from" ;;
      3) ExitMenu ;;
    esac
  done
}

sudo chmod 666 /dev/uinput
export SDL_GAMECONTROLLERCONFIG_FILE="/opt/inttools/gamecontrollerdb.txt"
if [[ ! -z $(pgrep -f gptokeyb) ]]; then
  pgrep -f gptokeyb | sudo xargs kill -9
fi
/opt/inttools/gptokeyb -1 "Update.sh" -c "/opt/inttools/keys.gptk" > /dev/null 2>&1 &

printf "\033c" > /dev/tty1
dialog --clear
trap ExitMenu EXIT
MainMenu
