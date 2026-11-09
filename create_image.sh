#!/bin/bash

# Only the image this build made (image_setup.sh's DISK) when there is one: the
# release images kept here as OTA bases (docs/OTA.md, two releases back) and the
# last test would otherwise be packed again by every build, 4 GB and minutes each.
if [ -n "${DISK:-}" ] && [ -f "${DISK}" ]; then
  IMAGES_TO_PACK="${DISK}"
else
  IMAGES_TO_PACK=$(ls *.img 2>/dev/null)
fi
for IMAGE in ${IMAGES_TO_PACK}
do
  # Remove stale archives so a rebuild always produces a fresh archive
  rm -f ${IMAGE}.7z ${IMAGE}.7z.*
  7z a -v1950m ${IMAGE}.7z ${IMAGE}
  #xz --keep -z -9 -T0 -M 80% ${IMAGE}
done
