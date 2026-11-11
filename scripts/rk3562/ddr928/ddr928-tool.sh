#!/bin/bash
#
# DDR Overclock (Options -> Tools): the RG52 Mini's memory at 928 MHz instead of 798.
#
# A dialog front end, in the way of Wifi.sh and Bluetooth.sh, to install-ddr928.sh, which
# writes the memory loader of the internal eMMC (the two idblock copies, sectors 64-2111):
# the 928 MHz loader only over the stock one it was made from, the stock one back, and
# every write verified by reading it back.
#  - Installed: says so, offers to remove it or to exit. Removing writes back the loader
#    area this tool saved before installing, when that backup is there and its sha256 is
#    the one noted then; otherwise the stock loader the installer carries. Then a restart.
#  - Not installed, and the loader is the stock one: offers to install, behind a warning
#    that has to be accepted. Then a restart.
#  - Anything else (another loader, no eMMC, not an RK3562): says why, and exit.
# A write that fails is followed by an offer to write the stock loader at once (--restore
# of the image the installer carries, whatever the eMMC holds by then).
# English, or Russian when ES's language is "ru".
#
# For testing, from the environment only: DDR928_LANG=en|ru; DDR928_DRYRUN=1 checks for
# real but writes nothing (a write "succeeds", no restart), DDR928_DRYRUN=fail makes the
# write fail; DDR928_FAKE=installed|stock|unknown|error pretends a state (and is a dry run).

# sbin for reboot: from an ssh session sudo finds nothing there otherwise.
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:$PATH

ENGINE=/usr/local/share/ddr928/install-ddr928.sh
ME=ddr928-tool.sh
BKDIR=/roms/backup/ddr928
META=pre-install-backup.sha256   # in $BKDIR: "<sha256>  <file>" of the backup made before installing
W=76

FAKE=${DDR928_FAKE:-}
DRY=${DDR928_DRYRUN:-}
[ -n "$FAKE" ] && [ -z "$DRY" ] && DRY=1

LNG=${DDR928_LANG:-}
if [ -z "$LNG" ]; then
  LNG=en
  grep -q '<string name="Language" value="ru"' /home/ark/.emulationstation/es_settings.cfg 2>/dev/null && LNG=ru
fi

# The messages. Those with values take them from the variables set when they are shown.
m() {
  case "$LNG:$1" in
  en:back) echo "DDR Overclock - the memory at 928 MHz instead of 798" ;;
  ru:back) echo "Разгон памяти: 928 МГц вместо 798" ;;
  en:title) echo "DDR Overclock" ;;
  ru:title) echo "Разгон памяти" ;;
  en:start) echo "Starting DDR Overclock.  Please wait..." ;;
  ru:start) echo "Запуск разгона памяти. Подождите..." ;;
  en:checking) echo "\nChecking the memory loader in the internal eMMC..." ;;
  ru:checking) echo "\nПроверяю загрузчик памяти во внутренней eMMC..." ;;
  en:select) echo "Select" ;;
  ru:select) echo "Выбрать" ;;
  en:exit) echo "Exit" ;;
  ru:exit) echo "Выход" ;;
  en:exit_btn) echo "Select + Start to Exit" ;;   # the menu's button, as in Wifi.sh and Bluetooth.sh
  ru:exit_btn) echo "Select + Start: выход" ;;
  en:ok) echo "OK" ;;
  ru:ok) echo "OK" ;;
  en:cancel) echo "Cancel" ;;
  ru:cancel) echo "Отмена" ;;

  en:st_installed) echo "The memory overclock is installed: the loader in the internal eMMC runs the memory at 928 MHz instead of the stock 798 MHz.${RUN:+\n\nThe memory runs at $RUN MHz now.}" ;;
  ru:st_installed) echo "Разгон памяти установлен: загрузчик во внутренней eMMC запускает память на 928 МГц вместо штатных 798 МГц.${RUN:+\n\nСейчас память работает на $RUN МГц.}" ;;
  en:pend_install) echo "The device has not been restarted since it was installed: 928 MHz takes effect after a restart." ;;
  ru:pend_install) echo "После установки устройство ещё не перезагружалось: 928 МГц заработают после перезагрузки." ;;
  en:do_remove) echo "Remove the overclock (back to 798 MHz)" ;;
  ru:do_remove) echo "Удалить разгон (вернуть 798 МГц)" ;;
  en:st_stock) echo "The memory overclock is not installed.\n\nIt raises the memory clock from 798 to 928 MHz by writing a modified memory loader to the internal eMMC. The loader in this device is the stock one the overclock was made from, so it can be installed." ;;
  ru:st_stock) echo "Разгон памяти не установлен.\n\nОн поднимает частоту памяти с 798 до 928 МГц: во внутреннюю eMMC записывается изменённый загрузчик памяти. Загрузчик этого устройства — тот самый штатный, из которого сделан разгон, поэтому установка возможна." ;;
  en:pend_remove) echo "The device has not been restarted since the overclock was removed: the memory runs at 928 MHz until a restart." ;;
  ru:pend_remove) echo "После удаления разгона устройство ещё не перезагружалось: до перезагрузки память работает на 928 МГц." ;;
  en:do_install) echo "Install the overclock (928 MHz)" ;;
  ru:do_install) echo "Установить разгон (928 МГц)" ;;
  en:st_unknown) echo "The overclock cannot be installed on this device.\n\nThe memory loader in its internal eMMC is not the stock version the overclock was made from (other firmware?). Writing over it could replace it with an older one, so the tool will not do it.\n\nNothing has been changed." ;;
  ru:st_unknown) echo "На это устройство разгон установить нельзя.\n\nЗагрузчик памяти в его внутренней eMMC — не та штатная версия, из которой сделан разгон (другая прошивка?). Запись поверх него могла бы заменить его более старым, поэтому утилита этого делать не будет.\n\nНичего не изменено." ;;
  en:st_error) echo "The overclock cannot be installed on this device:\n\n$ERR\n\nNothing has been changed." ;;
  ru:st_error) echo "На это устройство разгон установить нельзя:\n\n$ERR\n\nНичего не изменено." ;;
  en:lowbat) echo "The battery is at $CAP%.\n\nCharge it to at least 30% or connect the charger, then run DDR Overclock again." ;;
  ru:lowbat) echo "Заряд батареи $CAP%.\n\nЗарядите её хотя бы до 30% или подключите зарядку и запустите разгон памяти снова." ;;

  en:warn_title) echo "WARNING" ;;
  ru:warn_title) echo "ВНИМАНИЕ" ;;
  en:warning) echo "\Z1\ZbAT YOUR OWN RISK\Zn\n\nInstalling the overclock rewrites the memory loader in the internal eMMC: the first code the device runs when it is switched on, before it reads any SD card.\n\nIf anything goes wrong - the write is interrupted, or the memory of this particular device is not stable at 928 MHz - the device may no longer turn on at all, with any SD card. Bringing it back then takes a PC and taking the device apart: opening the back cover to put the board into MaskROM mode and writing the loader again with Rockchip's tools.\n\nYou do this at your own risk: nobody is responsible for a device that stops working.\n\nDo you accept the risk and want to install the overclock?" ;;
  ru:warning) echo "\Z1\ZbНА ВАШ СТРАХ И РИСК\Zn\n\nУстановка разгона перезаписывает загрузчик памяти во внутренней eMMC — первый код, который устройство выполняет при включении, ещё до чтения SD-карты.\n\nЕсли что-то пойдёт не так (запись прервётся или память именно этого устройства окажется нестабильной на 928 МГц), устройство может перестать включаться совсем, с любой SD-картой. Тогда для восстановления понадобятся компьютер и разборка устройства: снять заднюю крышку, перевести плату в режим MaskROM и заново записать загрузчик утилитами Rockchip.\n\nВы делаете это на свой страх и риск: никто не несёт ответственности за устройство, которое перестанет работать.\n\nВы принимаете этот риск и хотите установить разгон?" ;;
  en:accept) echo "I accept the risk" ;;
  ru:accept) echo "Принимаю риск" ;;
  en:installing) echo "\nInstalling the overclock: saving a backup and writing the loader to the eMMC...\n\nDo not turn the device off." ;;
  ru:installing) echo "\nУстанавливаю разгон: сохраняю резервную копию и записываю загрузчик в eMMC...\n\nНе выключайте устройство." ;;
  en:installed_ok) echo "The overclock is installed: the 928 MHz loader is written to the eMMC and verified.\n\n${BK:+The loader area as it was is saved in $BK; removing the overclock writes it back.\n\n}The memory runs at 928 MHz after a restart. Restart now?" ;;
  ru:installed_ok) echo "Разгон установлен: загрузчик 928 МГц записан в eMMC и проверен.\n\n${BK:+Прежнее содержимое области загрузчика сохранено в $BK; при удалении разгона будет записано оно.\n\n}Память заработает на 928 МГц после перезагрузки. Перезагрузить сейчас?" ;;
  en:restart) echo "Restart" ;;
  ru:restart) echo "Перезагрузить" ;;
  en:later) echo "Later" ;;
  ru:later) echo "Позже" ;;
  en:restarting) echo "\nRestarting..." ;;
  ru:restarting) echo "\nПерезагрузка..." ;;

  en:remove_q) echo "Remove the overclock?" ;;
  ru:remove_q) echo "Удалить разгон?" ;;
  en:src_backup) echo "The loader saved before the overclock was installed is written back ($BKFILE, its checksum matches)." ;;
  ru:src_backup) echo "Будет записан загрузчик, сохранённый перед установкой разгона ($BKFILE, контрольная сумма совпадает)." ;;
  en:src_stock) echo "The stock 798 MHz loader the installer carries is written back." ;;
  ru:src_stock) echo "Будет записан штатный загрузчик 798 МГц, встроенный в установщик." ;;
  en:src_bad) echo "The loader saved before the overclock was installed is damaged (its checksum does not match), so the stock 798 MHz loader the installer carries is written back instead." ;;
  ru:src_bad) echo "Загрузчик, сохранённый перед установкой разгона, повреждён (контрольная сумма не совпадает), поэтому вместо него будет записан штатный загрузчик 798 МГц, встроенный в установщик." ;;
  en:keep_on) echo "Do not turn the device off while it writes; it takes a few seconds." ;;
  ru:keep_on) echo "Не выключайте устройство во время записи, она займёт несколько секунд." ;;
  en:remove_yes) echo "Remove" ;;
  ru:remove_yes) echo "Удалить" ;;
  en:removing) echo "\nRemoving the overclock: writing the loader to the eMMC...\n\nDo not turn the device off." ;;
  ru:removing) echo "\nУдаляю разгон: записываю загрузчик в eMMC...\n\nНе выключайте устройство." ;;
  en:removed_ok) echo "The overclock is removed: the loader is written to the eMMC and verified.\n\nThe memory goes back to 798 MHz after a restart. Restart now?" ;;
  ru:removed_ok) echo "Разгон удалён: загрузчик записан в eMMC и проверен.\n\nПамять вернётся на 798 МГц после перезагрузки. Перезагрузить сейчас?" ;;

  en:failed_write) echo "Writing the loader failed:\n\n$ERR\n\nThe loader in the eMMC may be damaged. Do NOT restart or turn the device off.\n\nWrite the stock 798 MHz loader now?" ;;
  ru:failed_write) echo "Не удалось записать загрузчик:\n\n$ERR\n\nЗагрузчик в eMMC может быть повреждён. НЕ перезагружайте и не выключайте устройство.\n\nЗаписать штатный загрузчик 798 МГц прямо сейчас?" ;;
  en:writeback) echo "Write it" ;;
  ru:writeback) echo "Записать" ;;
  en:writing_stock) echo "\nWriting the stock 798 MHz loader to the eMMC...\n\nDo not turn the device off." ;;
  ru:writing_stock) echo "\nЗаписываю в eMMC штатный загрузчик 798 МГц...\n\nНе выключайте устройство." ;;
  en:stock_ok) echo "The stock 798 MHz loader is written to the eMMC and verified. Restart now?" ;;
  ru:stock_ok) echo "Штатный загрузчик 798 МГц записан в eMMC и проверен. Перезагрузить сейчас?" ;;
  en:stock_failed) echo "This write failed too:\n\n$ERR\n\nDo not turn the device off: it may not start again. Recovery needs MaskROM mode (see the dArkOS documentation)." ;;
  ru:stock_failed) echo "Эта запись тоже не удалась:\n\n$ERR\n\nНе выключайте устройство: оно может больше не включиться. Для восстановления нужен режим MaskROM (см. документацию dArkOS)." ;;
  en:failed_nowrite) echo "DDR Overclock stopped before writing anything:\n\n$ERR\n\nNothing has been changed." ;;
  ru:failed_nowrite) echo "Утилита остановилась, ничего не записав:\n\n$ERR\n\nНичего не изменено." ;;
  *) echo "$1" ;;
  esac
}

# The installer's errors in Russian, the ones a user can meet; others stay as they are.
err_text() {
  case "$LNG:$1" in
  en:"verification failed"*) echo "what was written did not read back the same"; return ;;
  ru:"verification failed"*) echo "записанное не совпало с образом при проверке чтением"; return ;;
  en:"write to "*" failed"*) echo "the write to the eMMC failed"; return ;;
  ru:"write to "*" failed"*) echo "запись в eMMC не удалась"; return ;;
  esac
  [ "$LNG" = ru ] || { echo "$1"; return; }
  case "$1" in
  "not an RK3562 device") echo "это не устройство на RK3562" ;;
  "internal eMMC not found") echo "внутренняя eMMC не найдена" ;;
  "cannot read the eMMC partition table") echo "не читается таблица разделов eMMC" ;;
  *"is read-only") echo "eMMC доступна только для чтения" ;;
  "no Rockchip loader"*) echo "в начале eMMC нет загрузчика Rockchip (RKNS)" ;;
  "first eMMC partition starts at sector"*) echo "первый раздел eMMC начинается внутри области загрузчика" ;;
  "the installer is missing"*) echo "нет установщика ($ENGINE)" ;;
  *) echo "$1" ;;
  esac
}

# The height a box needs for the text $1 at width $W, with $2 lines of its own (frame,
# buttons, list): its lines wrapped word by word as dialog does, a little narrower. dialog's
# own sizing (0) cut the text and the list. ${#} counts characters under the UTF-8 locale.
hgt() {
  local - t p word len n=0 w=$((W - 6))
  set -f
  t=$(printf '%b' "$(printf '%s' "$1" | sed 's/\\Z[0-9bBuUrRn]//g')")
  while IFS= read -r p; do
    len=0
    n=$((n + 1))
    for word in $p; do
      if [ $len -eq 0 ]; then
        len=${#word}
      elif [ $((len + 1 + ${#word})) -le $w ]; then
        len=$((len + 1 + ${#word}))
      else
        n=$((n + 1)); len=${#word}
      fi
    done
  done <<< "$t"
  n=$((n + $2))
  [ $n -gt 30 ] && n=30
  echo $n
}

info() { dialog --backtitle "$(m back)" --title "$(m title)" --infobox "$1" "$(hgt "$1" 2)" $W > /dev/tty1; }
msg() { dialog --backtitle "$(m back)" --title "$(m title)" --no-collapse --ok-label "${2:-$(m ok)}" --msgbox "$1" "$(hgt "$1" 6)" $W > /dev/tty1; }
ask() {   # $1 title, $2 text, $3 yes label, $4 no label, then dialog's options
  local t=$1 x=$2 y=$3 n=$4
  shift 4
  dialog --backtitle "$(m back)" --title "$t" --no-collapse "$@" --yes-label "$y" --no-label "$n" --yesno "$x" "$(hgt "$x" 6)" $W > /dev/tty1
}
menu() {   # $1 text, then the items; prints the choice
  local x=$1
  shift
  dialog --backtitle "$(m back)" --title "$(m title)" --no-collapse --ok-label "$(m select)" \
    --cancel-label "$(m exit_btn)" --menu "$x" "$(hgt "$x" 10)" $W 2 "$@" 2>&1 > /dev/tty1
}

pad_off() { pgrep -f gptokeyb | sudo xargs -r kill -9 2>/dev/null; }
pad_on() {
  pad_off
  sudo chmod 666 /dev/uinput
  export SDL_GAMECONTROLLERCONFIG_FILE="/opt/inttools/gamecontrollerdb.txt"
  /opt/inttools/gptokeyb -1 "$ME" -c "/opt/inttools/keys.gptk" > /dev/null 2>&1 &
  disown   # no "Killed" from bash when pad_off ends it
}

ExitMenu() {
  trap - EXIT
  printf "\033c" > /dev/tty1
  pad_off
  sudo setfont /usr/share/consolefonts/Lat7-Terminus20x10.psf.gz
  exit 0
}

# STATE installed|stock|unknown|error, ERR, PENDING (not restarted since), RUN (MHz now)
check_state() {
  local out rc f
  STATE=; ERR=; PENDING=0; RUN=
  f=$(tr ' ' '\n' < /sys/class/devfreq/dmc/available_frequencies 2>/dev/null | sort -n | tail -n 1)
  [ -n "$f" ] && RUN=$((f / 1000000))
  if [ -n "$FAKE" ]; then
    STATE=$FAKE
    [ "$STATE" = error ] && ERR=$(err_text "not an RK3562 device")
    return
  fi
  if [ ! -f "$ENGINE" ]; then
    STATE=error; ERR=$(err_text "the installer is missing: $ENGINE"); return
  fi
  out=$(cd /tmp && sudo sh "$ENGINE" --check 2>&1); rc=$?
  if [ $rc -ne 0 ]; then
    STATE=error; ERR=$(err_text "$(printf '%s\n' "$out" | sed -n 's/^ERROR: //p' | tail -n 1)"); return
  fi
  case "$out" in
  *"Result: 928 MHz is installed."*) STATE=installed ;;
  *"Result: can be installed"*) STATE=stock ;;
  *) STATE=unknown ;;
  esac
  case "$out" in
  *"has not been rebooted since"*|*"already has the stock loader"*) PENDING=1 ;;
  esac
}

# The installer with these arguments, from a copy in $BKDIR so that the backup it makes
# lands there; OUT, RC. Power key, sleep and shutdown held off while it runs.
run_engine() {
  local inh=()
  OUT=; RC=0
  if [ -n "$DRY" ]; then
    sleep 2
    if [ "$DRY" = fail ]; then
      OUT="ERROR: verification failed - do NOT reboot; run --restore with the backup"; RC=1
    else
      OUT="Written and verified by reading back. (dry run: nothing was written)"
    fi
    return
  fi
  if ! sudo mkdir -p "$BKDIR" 2>/dev/null; then
    BKDIR=/home/ark/ddr928-backup
    sudo mkdir -p "$BKDIR"
  fi
  if ! sudo cp "$ENGINE" "$BKDIR/install-ddr928.sh"; then
    OUT="ERROR: cannot copy the installer to $BKDIR"; RC=1; return
  fi
  if command -v systemd-inhibit > /dev/null 2>&1 &&
     sudo systemd-inhibit --what=shutdown:sleep --mode=block true > /dev/null 2>&1; then
    inh=(systemd-inhibit --what=shutdown:sleep:idle:handle-power-key:handle-suspend-key
         "--who=DDR Overclock" "--why=Writing the memory loader to the eMMC" --mode=block)
  fi
  OUT=$(cd /tmp && sudo "${inh[@]}" sh "$BKDIR/install-ddr928.sh" "$@" 2>&1); RC=$?
  sudo rm -f "$BKDIR/install-ddr928.sh"
}

engine_err() { ERR=$(printf '%s\n' "$OUT" | sed -n 's/^ERROR: //p' | tail -n 1); [ -n "$ERR" ] || ERR=$(printf '%s\n' "$OUT" | tail -n 1); }

battery_ok() {
  local st
  CAP=$(cat /sys/class/power_supply/battery/capacity 2>/dev/null)
  st=$(cat /sys/class/power_supply/battery/status 2>/dev/null)
  case "$CAP" in ''|*[!0-9]*) return 0 ;; esac
  if [ "$CAP" -lt 30 ] && [ "$st" != Charging ] && [ "$st" != Full ]; then
    msg "$(m lowbat)"
    return 1
  fi
  return 0
}

restart_now() {
  info "$(m restarting)"
  sync
  if [ -n "$DRY" ]; then
    sleep 2
    ExitMenu
  fi
  sudo reboot
  sleep 30
}

ask_restart() {   # $1 text
  if ask "$(m title)" "$1" "$(m restart)" "$(m later)"; then
    restart_now
  fi
  ExitMenu
}

# The pre-install backup, when it is there and its sha256 is the one noted: BKFILE.
# BKBAD=1 when it is there but does not match.
backup_ok() {
  local sha f
  BKFILE=; BKBAD=0
  [ -f "$BKDIR/$META" ] || return 1
  read -r sha f < "$BKDIR/$META"
  if [ -n "$f" ] && [ -f "$BKDIR/$f" ] &&
     [ "$(sudo sha256sum "$BKDIR/$f" | cut -d' ' -f1)" = "$sha" ]; then
    BKFILE=$BKDIR/$f
    return 0
  fi
  BKBAD=1
  return 1
}

# The stock loader the installer carries, written whatever the eMMC holds now.
write_stock() {
  local img want
  pad_off
  info "$(m writing_stock)"
  if [ -n "$DRY" ]; then
    DRY=1 run_engine --restore stock --yes
  else
    img=$(mktemp /tmp/ddr928-stock.XXXXXX)
    want=$(sed -n 's/^STOCK_SHA=//p' "$ENGINE")
    sed -n '/^__STOCK_BEGIN__$/,/^__STOCK_END__$/p' "$ENGINE" | sed '1d;$d' |
      base64 -d 2>/dev/null | gzip -dc > "$img" 2>/dev/null
    if [ -n "$want" ] && [ "$(sha256sum < "$img" | cut -d' ' -f1)" = "$want" ]; then
      run_engine --restore "$img" --yes
    else
      OUT="ERROR: the stock loader in $ENGINE is damaged"; RC=1
    fi
    rm -f "$img"
  fi
  pad_on
  if [ $RC -eq 0 ]; then
    ask_restart "$(m stock_ok)"
  fi
  engine_err
  ERR=$(err_text "$ERR")
  msg "$(m stock_failed)"
  ExitMenu
}

write_failed() {
  engine_err
  case "$ERR" in
  *"do NOT reboot"*)
    ERR=$(err_text "$ERR")
    if ask "$(m title)" "$(m failed_write)" "$(m writeback)" "$(m exit)"; then
      write_stock
    fi
    ExitMenu ;;
  *)
    ERR=$(err_text "$ERR")
    msg "$(m failed_nowrite)" ;;
  esac
}

do_install() {
  local sha
  battery_ok || return
  ask "$(m warn_title)" "$(m warning)" "$(m accept)" "$(m cancel)" --colors --defaultno || return
  pad_off
  info "$(m installing)"
  run_engine --yes
  pad_on
  if [ $RC -ne 0 ]; then
    write_failed
    return
  fi
  # The backup of the loader area the installer made before writing: removing the
  # overclock writes it back, while its sha256 still matches the one noted here.
  BK=$(printf '%s\n' "$OUT" | sed -n 's/^Backup of the whole loader area: //p' | tail -n 1)
  sha=$(printf '%s\n' "$OUT" | sed -n 's/^  sha256 \([0-9a-f]\{64\}\)$/\1/p' | tail -n 1)
  if [ -n "$BK" ] && [ -n "$sha" ] && [ "$(dirname "$BK")" = "$BKDIR" ]; then
    printf '%s  %s\n' "$sha" "$(basename "$BK")" | sudo tee "$BKDIR/$META" > /dev/null
  else
    BK=
  fi
  ask_restart "$(m installed_ok)"
}

do_remove() {
  local src
  battery_ok || return
  if backup_ok; then
    src=$(m src_backup)
  elif [ "$BKBAD" = 1 ]; then
    src=$(m src_bad)
  else
    src=$(m src_stock)
  fi
  ask "$(m title)" "$(m remove_q)\n\n$src\n\n$(m keep_on)" "$(m remove_yes)" "$(m cancel)" --defaultno || return
  pad_off
  info "$(m removing)"
  if [ -n "$BKFILE" ]; then
    run_engine --restore "$BKFILE" --yes
    # A backup the installer turns down before writing (no RKNS, too short) is not a
    # failed write: the stock loader instead.
    if [ $RC -ne 0 ] && ! printf '%s\n' "$OUT" | grep -q 'do NOT reboot'; then
      run_engine --uninstall --yes
    fi
  else
    run_engine --uninstall --yes
  fi
  pad_on
  if [ $RC -ne 0 ]; then
    write_failed
    return
  fi
  ask_restart "$(m removed_ok)"
}

MainMenu() {
  local text sel
  while true; do
    info "$(m checking)"
    check_state
    case "$STATE" in
    installed)
      text=$(m st_installed)
      [ "$PENDING" = 1 ] && text="$text\n\n$(m pend_install)"
      sel=$(menu "$text" 1 "$(m do_remove)" 2 "$(m exit)") || ExitMenu
      if [ "$sel" = 1 ]; then do_remove; else ExitMenu; fi ;;
    stock)
      text=$(m st_stock)
      [ "$PENDING" = 1 ] && text="$text\n\n$(m pend_remove)"
      sel=$(menu "$text" 1 "$(m do_install)" 2 "$(m exit)") || ExitMenu
      if [ "$sel" = 1 ]; then do_install; else ExitMenu; fi ;;
    unknown)
      msg "$(m st_unknown)" "$(m exit)"
      ExitMenu ;;
    *)
      msg "$(m st_error)" "$(m exit)"
      ExitMenu ;;
    esac
  done
}

sudo chmod 666 /dev/tty1
printf "\033c" > /dev/tty1
printf "\033%%G" > /dev/tty1      # the console in UTF-8, for the Russian text
printf "\e[?25l" > /dev/tty1      # no cursor
export TERM=linux
export LC_ALL=C.UTF-8
export XDG_RUNTIME_DIR=/run/user/$UID/
# Terminus Bold 22x11 like Wifi.sh's, in the Uni2 set: it has Cyrillic.
sudo setfont /usr/share/consolefonts/Uni2-TerminusBold22x11.psf.gz
dialog --clear
printf "%s" "$(m start)" > /dev/tty1

trap ExitMenu EXIT
trap 'exit 0' TERM INT HUP
pad_on
MainMenu
