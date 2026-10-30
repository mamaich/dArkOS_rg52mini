#!/bin/bash
# ROMs over USB from a PC, by MTP: a PC sees the console as a media device, as it
# does an Android phone, and copies files with its file manager (docs/KNOWN-ISSUES.md,
# "USB: ROMs from a PC over MTP"). darkos-mtp.service runs this as root around
# umtprd (umtp-responder), which serves the files as ark:
#   setup     the USB gadget in configfs, one FunctionFS function mounted for ark,
#             and umtprd's configuration in /run
#   bind      the gadget bound to the port's controller, once umtprd has written
#             its descriptors (the endpoint files appear then)
#   unbind    the gadget off the controller, before umtprd is stopped
#   teardown  all of it undone
#   kick      a restart after the cable was pulled (99-darkos-mtp.rules)
# The port stays a host for USB devices when one is plugged in: the controller
# switches roles by itself, and the gadget only answers a PC.
G=/sys/kernel/config/usb_gadget/darkos
F=/dev/ffs-mtp
R=/run/darkos-mtp
case "${1:-}" in
setup)
  "$0" teardown
  mkdir -p $R
  # The name a PC shows: the model from the device tree without its vendor
  # ("AISLPC RG52 Mini" -> "RG52 Mini"). The serial is the board's with a prefix,
  # so a PC does not take it for another system on the same board (GammaOS's MTP)
  # and its memory of an earlier name does not stick.
  model=$(tr -d '\0' < /proc/device-tree/model 2>/dev/null)
  case "$model" in *" "*) model=${model#* } ;; esac
  model=${model:-dArkOS device}
  serial=$(tr -d '\0' < /proc/device-tree/serial-number 2>/dev/null)
  [ -n "$serial" ] || serial=$(cut -c1-16 /etc/machine-id)
  serial="dArkOS-$serial"
  ver=$(cat /usr/share/plymouth/themes/text.plymouth 2>/dev/null | sed -n 's/^title=dArkOS //p')
  {
    echo 'loop_on_disconnect 1'
    echo 'storage "/roms" "ROMs" "rw"'
    mountpoint -q /roms2 && echo 'storage "/roms2" "ROMs (SD2)" "rw"'
    echo 'umask 002'
    echo 'show_hidden_files 0'
    echo 'manufacturer "dArkOS"'
    echo "product \"$model\""
    echo "serial \"$serial\""
    echo "firmware_version \"${ver:-dArkOS}\""
    echo 'interface "MTP"'
    echo 'usb_vendor_id 0x1D6B'
    echo 'usb_product_id 0x0100'
    echo 'usb_class 0x6'
    echo 'usb_subclass 0x1'
    echo 'usb_protocol 0x1'
    echo 'usb_dev_version 0x3008'
    echo 'usb_functionfs_mode 0x1'
    echo "usb_dev_path \"$F/ep0\""
    echo "usb_epin_path \"$F/ep1\""
    echo "usb_epout_path \"$F/ep2\""
    echo "usb_epint_path \"$F/ep3\""
    echo 'usb_max_packet_size 0x200'
  } > $R/umtprd.conf
  mkdir $G || exit 1
  echo 0x1d6b > $G/idVendor
  echo 0x0100 > $G/idProduct
  echo 0x0100 > $G/bcdDevice
  echo 0x0200 > $G/bcdUSB
  mkdir $G/strings/0x409
  echo "$serial" > $G/strings/0x409/serialnumber
  echo dArkOS > $G/strings/0x409/manufacturer
  echo "$model" > $G/strings/0x409/product
  mkdir -p $G/configs/c.1/strings/0x409
  echo MTP > $G/configs/c.1/strings/0x409/configuration
  echo 500 > $G/configs/c.1/MaxPower
  mkdir $G/functions/ffs.mtp
  ln -s $G/functions/ffs.mtp $G/configs/c.1/
  mkdir -p $F
  mount -t functionfs mtp $F -o uid=$(id -u ark),gid=$(id -g ark)
  ;;
bind)
  for i in $(seq 1 50); do [ -e $F/ep1 ] && break; sleep 0.1; done
  ls /sys/class/udc | head -1 > $G/UDC
  ;;
unbind)
  # first when the service stops: after a cable pulled during a copy, umtprd sits
  # in a FunctionFS read that no signal ends, and only the unbinding returns it
  [ -d $G ] && echo > $G/UDC 2>/dev/null
  exit 0
  ;;
teardown)
  if [ -d $G ]; then
    echo > $G/UDC 2>/dev/null
    rm -f $G/configs/c.1/ffs.mtp
    rmdir $G/configs/c.1/strings/0x409 $G/configs/c.1 $G/functions/ffs.mtp \
          $G/strings/0x409 $G 2>/dev/null
  fi
  mountpoint -q $F && umount $F
  exit 0
  ;;
kick)
  # 99-darkos-mtp.rules, at every USB disconnect: umtprd 1.3.10 does not recover
  # from a cable pulled in the middle of a copy (the port comes back configured,
  # and the PC gets no answer), so it is started afresh. Not within 5 s of its own
  # start, whose unbinding and binding of the gadget send the same event.
  systemctl -q is-active darkos-mtp || exit 0
  t=$(systemctl show -p ActiveEnterTimestampMonotonic --value darkos-mtp)
  now=$(awk '{printf "%d", $1 * 1000000}' /proc/uptime)
  [ $((now - t)) -gt 5000000 ] && systemctl --no-block restart darkos-mtp
  exit 0
  ;;
*)
  echo "usage: $0 setup|bind|unbind|teardown|kick" >&2; exit 2
  ;;
esac
