#!/bin/bash
# Turn the BIOS check at game start back on (darkos-bios-check): before a game starts, the BIOS
# files its emulator lacks are listed with the folder they go to.
printf "\033c" >> /dev/tty1
rm -f /home/ark/.config/.NOBIOSCHECK
printf "\n\n\n\e[32mBIOS check enabled.\n\nBefore a game starts, the BIOS files it lacks\nare listed with the folder they go to.\n" > /dev/tty1
sudo cp "/usr/local/bin/Disable BIOS Check.sh" "/opt/system/Advanced/"
sudo rm "/opt/system/Advanced/Enable BIOS Check.sh"
sleep 3
printf "\033c" >> /dev/tty1
sudo systemctl restart emulationstation
