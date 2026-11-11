#!/bin/bash
# The retrorun and retrorun32 branches below are those of retrorun/scripts/saturn.sh (the build installs
# this file over that one): retrorun's config, --triggers and DEVICE_NAME, which on the RG52 Mini picks
# the profile that shows the picture the right way up. As upstream's 06738b308 (2026-11-08).

ESUDO=""

if [[ $1 == *"standalone"* ]]; then
  directory=$(dirname "$2" | cut -d "/" -f2)
  if [[ ! -d "/$directory/saturn/yabasanshiro" ]]; then
    mkdir /$directory/saturn/yabasanshiro
  fi
  cd /opt/yabasanshiro
  if [[ ! -f "input.cfg" ]]; then
    if [[ -f "keymapv2.json" ]]; then
      rm -f keymapv2.json
    fi
    cp -f /etc/emulationstation/es_input.cfg input.cfg
  fi
  echo "VAR=yaba" > /home/ark/.config/KILLIT
  sudo systemctl restart killer_daemon.service
  if [[ $1 == "standalone-bios" ]]; then
    if [[ ! -f "/$directory/bios/saturn_bios.bin" ]]; then
      printf "\033c" >> /dev/tty1
      printf "\033[1;33m" >> /dev/tty1
      printf "\n I don't detect a saturn_bios.bin bios file in the" >> /dev/tty1
      printf "\n /$directory/bios folder.  Either place one in that" >> /dev/tty1
      printf "\n location or switch to the standalone-nobios emulator." >> /dev/tty1
      sleep 10
      printf "\033[0m" >> /dev/tty1
    else
      ./yabasanshiro -r 3 -i "$2" -b /$directory/bios/saturn_bios.bin
    fi
  else
    ./yabasanshiro -r 3 -i "$2"
  fi
  sudo systemctl stop killer_daemon.service
  sudo systemctl restart ogage &
  cd ~
elif  [[ $1 == "retroarch" ]]; then
  /usr/local/bin/"$1" -L /home/ark/.config/"$1"/cores/"$2"_libretro.so "$3"
elif [[ $1 == "retroarch32" ]]; then
  /usr/local/bin/"$1" -L /home/ark/.config/"$1"/cores/"$2"_libretro.so "$3"
elif [[ $1 == "retrorun" ]]; then
  directory=$(dirname "$3" | cut -d "/" -f2)
  if [[ ! -f "/$directory/bios/saturn_bios.bin" ]]; then
    printf "\033c" >> /dev/tty1
    printf "\033[1;33m" >> /dev/tty1
    printf "\n I don't detect a saturn_bios.bin bios file in the" >> /dev/tty1
    printf "\n /$directory/bios folder.  Either place one in that" >> /dev/tty1
    printf "\n location or switch to the standalone-nobios emulator." >> /dev/tty1
    sleep 10
    printf "\033[0m" >> /dev/tty1
  fi
  if [[ -e "/dev/input/by-path/platform-ff300000.usb-usb-0:1.2:1.0-event-joystick" ]]; then
    #sudo rm /dev/input/by-path/platform-odroidgo2-joypad-event-joystick || true
    #echo 'creating fake joypad'
    #sudo /usr/local/bin/rg351p-js2xbox --silent -t oga_joypad &
    #sleep 0.2
    #sudo ln -s /dev/input/event4 /dev/input/by-path/platform-odroidgo2-joypad-event-joystick
    #sudo chmod 777 /dev/input/by-path/platform-odroidgo2-joypad-event-joystick
    #sleep 0.2
    ESUDO="sudo --preserve-env=DEVICE_NAME"
    DEVICENAME="RG351V"
  elif compgen -G "/boot/rk3562*" > /dev/null; then
    # RK3562 (RG52 Mini / RG43H Pro) -- follows the c204989 pattern. The gamepad is
    # the rk3562-joystick (play_joystick) driver, not any rk3566/rk3326 pad. retrorun
    # reads its screen resolution from libgo2/DRM, not from DEVICE_NAME; DEVICE_NAME only
    # selects device quirks (rotation etc.). Both RK3562 panels are presented landscape,
    # so use a non-rotated profile (RG351P keeps hasDeviceRotatedScreen() == false).
    # The RG52 Mini's panel is mounted the other way round: our retrorun has
    # an RG52MINI profile that turns the RG351P picture half a turn.
    if [ "$(cat /home/ark/.config/.DEVICE 2>/dev/null)" == "RG52MINI" ]; then
      DEVICENAME="RG52MINI"
    else
      DEVICENAME="RG351P"
    fi
  elif compgen -G "/boot/rk3566*" > /dev/null; then
    if test ! -z "$(grep "RG353V" /home/ark/.config/.DEVICE | tr -d '\0')"
    then
      DEVICENAME="RG353V"
    elif test ! -z "$(grep "RG353M" /home/ark/.config/.DEVICE | tr -d '\0')"
    then
      DEVICENAME="RG353M"
    else
      DEVICENAME="RG503"
    fi
  elif [ -f "/boot/rk3326-r33s-linux.dtb" ] || [ -f "/boot/rk3326-r35s-linux.dtb" ] || [ -f "/boot/rk3326-r36s-linux.dtb" ] || [ -f "/boot/rk3326-rg351mp-linux.dtb" ] || [ -f "/boot/rk3326-g350-linux.dtb" ]; then
    DEVICENAME="RG351MP"
  elif [ -f "/boot/rk3326-gameforce-linux.dtb" ]; then
    DEVICENAME="RG351MP"
  elif [ -f "/boot/rk3326-odroidgo2-linux.dtb" ] || [ -f "/boot/rk3326-odroidgo2-linux-v11.dtb" ] || [ -f "/boot/rk3326-odroidgo3-linux.dtb" ]; then
    DEVICENAME="RGB10"
  else
    DEVICENAME="RG351P"
  fi
  export DEVICE_NAME="${DEVICENAME}"

  #CURRUMBLESET="$(grep "retrorun_rumble_type = " /home/ark/.config/retrorun.cfg | cut -c24-)"

  #if [[ ${DEVICENAME} == "RG503" ]]; then
    #if [[ ${CURRUMBLESET} != "event" ]]; then
      #sed -i "/retrorun_rumble_type \=/c\retrorun_rumble_type \= event" /home/ark/.config/retrorun.cfg
    #fi
  #else
    #if [[ ${CURRUMBLESET} != "pwm" ]]; then
      #sed -i "/retrorun_rumble_type \=/c\retrorun_rumble_type \= pwm" /home/ark/.config/retrorun.cfg
    #fi
  #fi

  directory=$(dirname "$3" | cut -d "/" -f2)
  CURDIRECTORYSET="$(grep "retrorun_screenshot_folder = " /home/ark/.config/retrorun.cfg | cut -d "/" -f2-3)"
  if [[ "${CURDIRECTORYSET}" != "${directory}/saturn" ]]; then
    sed -i "/retrorun_screenshot_folder \=/c\retrorun_screenshot_folder \= \/$directory\/saturn" /home/ark/.config/retrorun.cfg
  fi

  $ESUDO /usr/local/bin/retrorun -c /home/ark/.config/retrorun.cfg --triggers -s /$directory/saturn -d /$directory/bios /home/ark/.config/retroarch/cores/"$2"_libretro.so "$3"

  if [[ -e "/dev/input/by-path/platform-ff300000.usb-usb-0:1.2:1.0-event-joystick" ]]; then
    sleep 0.5
    sudo rm /dev/input/by-path/platform-odroidgo2-joypad-event-joystick
    sudo kill $(pidof rg351p-js2xbox)
  fi
else
  directory=$(dirname "$3" | cut -d "/" -f2)
  if [[ ! -f "/$directory/bios/saturn_bios.bin" ]]; then
    printf "\033c" >> /dev/tty1
    printf "\033[1;33m" >> /dev/tty1
    printf "\n I don't detect a saturn_bios.bin bios file in the" >> /dev/tty1
    printf "\n /$directory/bios folder.  Either place one in that" >> /dev/tty1
    printf "\n location or switch to the standalone-nobios emulator." >> /dev/tty1
    sleep 10
    printf "\033[0m" >> /dev/tty1
  fi
  if [[ -e "/dev/input/by-path/platform-ff300000.usb-usb-0:1.2:1.0-event-joystick" ]]; then
    #sudo rm /dev/input/by-path/platform-odroidgo2-joypad-event-joystick || true
    #echo 'creating fake joypad'
    #sudo /usr/local/bin/rg351p-js2xbox --silent -t oga_joypad &
    #sleep 0.2
    #sudo ln -s /dev/input/event4 /dev/input/by-path/platform-odroidgo2-joypad-event-joystick
    #sudo chmod 777 /dev/input/by-path/platform-odroidgo2-joypad-event-joystick
    #sleep 0.2
    ESUDO="sudo --preserve-env=DEVICE_NAME"
    DEVICENAME="RG351V"
  elif compgen -G "/boot/rk3562*" > /dev/null; then
    # RK3562 (RG52 Mini / RG43H Pro) -- follows the c204989 pattern. The gamepad is
    # the rk3562-joystick (play_joystick) driver, not any rk3566/rk3326 pad. retrorun
    # reads its screen resolution from libgo2/DRM, not from DEVICE_NAME; DEVICE_NAME only
    # selects device quirks (rotation etc.). Both RK3562 panels are presented landscape,
    # so use a non-rotated profile (RG351P keeps hasDeviceRotatedScreen() == false).
    # The RG52 Mini's panel is mounted the other way round: our retrorun has
    # an RG52MINI profile that turns the RG351P picture half a turn.
    if [ "$(cat /home/ark/.config/.DEVICE 2>/dev/null)" == "RG52MINI" ]; then
      DEVICENAME="RG52MINI"
    else
      DEVICENAME="RG351P"
    fi
  elif compgen -G "/boot/rk3566*" > /dev/null; then
    if test ! -z "$(grep "RG353V" /home/ark/.config/.DEVICE | tr -d '\0')"
    then
      DEVICENAME="RG353V"
    elif test ! -z "$(grep "RG353M" /home/ark/.config/.DEVICE | tr -d '\0')"
    then
      DEVICENAME="RG353M"
    else
      DEVICENAME="RG503"
    fi
  elif [ -f "/boot/rk3326-r33s-linux.dtb" ] || [ -f "/boot/rk3326-r35s-linux.dtb" ] || [ -f "/boot/rk3326-r36s-linux.dtb" ] || [ -f "/boot/rk3326-rg351mp-linux.dtb" ] || [ -f "/boot/rk3326-g350-linux.dtb" ]; then
    DEVICENAME="RG351MP"
  elif [ -f "/boot/rk3326-gameforce-linux.dtb" ]; then
    DEVICENAME="RG351MP"
  elif [ -f "/boot/rk3326-odroidgo2-linux.dtb" ] || [ -f "/boot/rk3326-odroidgo2-linux-v11.dtb" ] || [ -f "/boot/rk3326-odroidgo3-linux.dtb" ]; then
    DEVICENAME="RGB10"
  else
    DEVICENAME="RG351P"
  fi
  export DEVICE_NAME="${DEVICENAME}"

  #CURRUMBLESET="$(grep "retrorun_rumble_type = " /home/ark/.config/retrorun.cfg | cut -c24-)"

  #if [[ ${DEVICENAME} == "RG503" ]]; then
    #if [[ ${CURRUMBLESET} != "event" ]]; then
      #sed -i "/retrorun_rumble_type \=/c\retrorun_rumble_type \= event" /home/ark/.config/retrorun.cfg
    #fi
  #else
    #if [[ ${CURRUMBLESET} != "pwm" ]]; then
      #sed -i "/retrorun_rumble_type \=/c\retrorun_rumble_type \= pwm" /home/ark/.config/retrorun.cfg
    #fi
  #fi

  directory=$(dirname "$3" | cut -d "/" -f2)
  CURDIRECTORYSET="$(grep "retrorun_screenshot_folder = " /home/ark/.config/retrorun.cfg | cut -d "/" -f2-3)"
  if [[ "${CURDIRECTORYSET}" != "${directory}/saturn" ]]; then
    sed -i "/retrorun_screenshot_folder \=/c\retrorun_screenshot_folder \= \/$directory\/saturn" /home/ark/.config/retrorun.cfg
  fi

  $ESUDO /usr/local/bin/retrorun32 -c /home/ark/.config/retrorun.cfg --triggers -s /$directory/saturn -d /$directory/bios /home/ark/.config/retroarch32/cores/"$2"_libretro.so "$3"

  if [[ -e "/dev/input/by-path/platform-ff300000.usb-usb-0:1.2:1.0-event-joystick" ]]; then
    sleep 0.5
    sudo rm /dev/input/by-path/platform-odroidgo2-joypad-event-joystick
    sudo kill $(pidof rg351p-js2xbox)
  fi
fi
