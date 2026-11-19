#!/bin/bash
#
# DRM connector setup and HDMI detection for RK3562 (RG52 Mini)
# Creates /var/run/drmConn (required by EmulationStation for Display Settings menu)
# and /var/run/drmMode (used by RetroArch for display mode selection).
# Runs at boot via @reboot crontab; hdmi-hotplug.sh (udev) handles the cable after boot.
#

# Default: internal DSI panel (connector 0, mode 0)
echo 0 | sudo tee /var/run/drmConn > /dev/null
echo 0 | sudo tee /var/run/drmMode > /dev/null

# Check if HDMI is connected via RK628 bridge
HDMI_STATUS=$(cat /sys/class/drm/card0-HDMI-A-1/status 2>/dev/null)

if [ "$HDMI_STATUS" == "connected" ]; then
    echo 1 | sudo tee /var/run/drmConn > /dev/null
    # The panel's connector reads "disconnected" while HDMI is chosen, so that Arm's Vulkan
    # driver, which offers the first connected connector, offers HDMI (hdmi-hotplug.sh).
    echo off | sudo tee /sys/class/drm/card0-DSI-1/status > /dev/null
fi

# The text console (the tools in Options: Wifi, Bluetooth and the rest) draws in the buffer
# the kernel made for the screen it found at boot. The panel's is 720x1280, which
# fbcon=rotate:1 (extlinux.conf) turns into landscape. With HDMI in at boot it is the TV's
# 1920x1080, and that rotation put the tools on their side on the TV: a landscape buffer is
# drawn unrotated. The buffer keeps its size until the next boot, so this is decided here,
# once. (The other screen, after a cable is plugged in or pulled out, shows no console at
# all: the buffer does not fit its mode.)
read -r FB_W FB_H < <(tr ',' ' ' < /sys/class/graphics/fb0/virtual_size 2>/dev/null)
if [ "${FB_W:-0}" -gt "${FB_H:-0}" ]; then
    echo 0 | sudo tee /sys/class/graphics/fbcon/rotate_all > /dev/null
fi
