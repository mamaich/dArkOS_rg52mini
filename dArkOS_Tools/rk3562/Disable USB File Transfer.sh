#!/bin/bash
# Turn off ROMs over USB (MTP, darkos-mtp.service): stopped now and not started at
# boot, so a PC only charges the console. "Enable USB File Transfer" turns it on.
printf "\033c" >> /dev/tty1
sudo systemctl disable --now darkos-mtp
printf "\n\n\n\e[32mUSB file transfer disabled.\n\nA PC only charges the console now.\n" > /dev/tty1
sudo cp "/usr/local/bin/Enable USB File Transfer.sh" "/opt/system/"
sudo rm "/opt/system/Disable USB File Transfer.sh"
sleep 3
printf "\033c" >> /dev/tty1
sudo systemctl restart emulationstation
