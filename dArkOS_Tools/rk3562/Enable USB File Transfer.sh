#!/bin/bash
# Turn on ROMs over USB (MTP, darkos-mtp.service): a PC shows the console as a media
# device with a ROMs folder, as it does an Android phone.
printf "\033c" >> /dev/tty1
sudo systemctl enable --now darkos-mtp
printf "\n\n\n\e[32mUSB file transfer enabled.\n\nConnect the console to a PC: it shows up as a\nmedia device with a ROMs folder. New games appear\nin the lists after SELECT > UPDATE GAMES LISTS.\n" > /dev/tty1
sudo cp "/usr/local/bin/Disable USB File Transfer.sh" "/opt/system/"
sudo rm "/opt/system/Enable USB File Transfer.sh"
sleep 5
printf "\033c" >> /dev/tty1
sudo systemctl restart emulationstation
