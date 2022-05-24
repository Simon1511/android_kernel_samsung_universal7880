#!/sbin/sh
#
# ADDOND_VERSION=2
#
# /system/addon.d/98-risekernel.sh
# During an OTA upgrade, this script backs up /dev/block/platform/13540000.dwmmc0/by-name/BOOT,
# /system is formatted and reinstalled, then the file is restored.
# Based on LineageOS' addon.d script, modified by Simon1511@XDA
#

. /tmp/backuptool.functions &

buildDate=$(date '+%Y%m%d')

# This file only exists if the user is rooted with Magisk
# We don't want Magisk to override riseKernel
if [ -f /tmp/addon.d/99-magisk.sh ]; then
    chmod -x /tmp/addon.d/99-magisk.sh
fi

case "$1" in
  backup)
    # Backup of the actual kernel
    if grep -qs "/data" /proc/mounts; then
        umount /data
    fi
    mount /data
    dd if=/dev/block/platform/13540000.dwmmc0/by-name/BOOT of=/data/risekernel-"$buildDate".img
  ;;
  restore)
    # Restore riseKernel
    if grep -qs "/data" /proc/mounts; then
        umount /data
    fi
    mount /data
    sleep 5 && dd if=/data/risekernel-"$buildDate".img of=/dev/block/platform/13540000.dwmmc0/by-name/BOOT &
  ;;
  pre-backup)
    # Stub
  ;;
  post-backup)
    # Stub
  ;;
  pre-restore)
    # Stub
  ;;
  post-restore)
    # Stub
  ;;
esac
