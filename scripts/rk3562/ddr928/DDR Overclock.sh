#!/bin/bash
# Options -> Tools -> DDR Overclock (RG52 Mini): the memory at 928 MHz instead of 798.
# The tool itself is /usr/local/bin/ddr928-tool.sh, part of the system; this file starts it.
if [ -x /usr/local/bin/ddr928-tool.sh ]; then
  exec /usr/local/bin/ddr928-tool.sh "$@"
fi
sudo chmod 666 /dev/tty1
printf "\033c\n\n  DDR Overclock needs dArkOS 1.8 or later for the RG52 Mini:\n  /usr/local/bin/ddr928-tool.sh is missing.\n" > /dev/tty1
sleep 5
