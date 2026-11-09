#!/bin/bash
# Turn off the BIOS check at game start (darkos-bios-check): games start without it, and a
# RetroArch that quits at once no longer shows its log. "Enable BIOS Check" turns it back on.
printf "\033c" >> /dev/tty1
touch /home/ark/.config/.NOBIOSCHECK
printf "\n\n\n\e[32mBIOS check disabled.\n\nGames start without a check of their BIOS files,\nand a game that fails to start says nothing.\n" > /dev/tty1
sudo cp "/usr/local/bin/Enable BIOS Check.sh" "/opt/system/Advanced/"
sudo rm "/opt/system/Advanced/Disable BIOS Check.sh"
sleep 3
printf "\033c" >> /dev/tty1
sudo systemctl restart emulationstation
