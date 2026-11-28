#!/bin/bash
# 1.14: images 1.9 to 1.12 shipped a PortMaster installer that installs the beta channel
# (docs/KNOWN-ISSUES.md, "PortMaster: images 1.9 to 1.12 installed the beta channel"): the stable
# release's Install.PortMaster.sh with the latest release's PortMaster.zip put into it by their
# pm-installer-update.py. It is in roms/tools, on the ROMs partition, which an update never
# touches (make-ota leaves roms.tar out), so a card updated from those images has it still, and
# installs the beta whenever PortMaster goes on that card for the first time or again (runport.sh
# before the first port, or Tools). Here such an installer is replaced by the image's own, the
# stable release's, kept on the root for this at /usr/share/darkos/portmaster
# (finishing_touches-rk3562.sh). It is one of those when its header is the 2026.06 stable
# installer's but for the four fields that tool rewrote (the payload's sizes, its MD5, and the
# CRC, which it set to 0000000000); any other installer (the image's, one the user put there) is
# left alone. A PortMaster installed already keeps its channel (KNOWN-ISSUES says how to go back).
# The owner, 2026-11-28.
#
# Not in the merged list: /roms is no part of the snapshot, so a rollback would delete the file
# rather than put the old one back. Never fails the update; safe to run twice.
PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
F=/roms/tools/Install.PortMaster.sh
N=/usr/share/darkos/portmaster/Install.PortMaster.sh
[ -f "$F" ] && [ -f "$N" ] || exit 0
cmp -s "$F" "$N" && exit 0
python3 - "$F" <<'PY' || exit 0
import hashlib, re, sys
# The header of the stable 2026.06.23-0015 release's Install.PortMaster.sh, with filesizes,
# totalsize, MD5 and CRCsum emptied: what each installer of images 1.9 to 1.12 kept of it
# (1.12's, which installs 2026.11.17-0700, checked against 1.13's on 2026-11-28).
STABLE_2026_06 = "cb3e743466966ceb802fda49d93655828430d2e72744ced89402f56f9c6870a9"
d = open(sys.argv[1], "rb").read(65536)
skip = re.search(rb'^skip="(\d+)"$', d, re.M)
if not skip or not re.search(rb'^CRCsum="0000000000"$', d, re.M):
    sys.exit(1)
off = 0
for _ in range(int(skip.group(1))):
    off = d.index(b"\n", off) + 1
h = d[:off]
for k in (b"filesizes", b"totalsize", b"MD5", b"CRCsum"):
    h = re.sub(rb'^(' + k + rb')="[^"\n]*"$', rb'\1=""', h, count=1, flags=re.M)
sys.exit(0 if hashlib.sha256(h).hexdigest() == STABLE_2026_06 else 1)
PY
# The new content under the old file's owner, mode and ACL: on a dual-boot card /roms is Android's
# ext4 (ark:media_rw, 770, a default ACL on the folders that a file made here would take, and the
# console's cp leaves it); on exFAT the mount gives every file the same, and cp cannot set them
# (it still makes the file).
tmp=/roms/tools/.ota-tmp.Install.PortMaster.sh
rm -f "$tmp"
cp -p --attributes-only "$F" "$tmp" 2>/dev/null
[ -e "$tmp" ] && python3 - "$F" "$tmp" <<'PY'
import os, sys
src, dst, a = sys.argv[1], sys.argv[2], "system.posix_acl_access"
try:
    v = os.getxattr(src, a)
except OSError:
    v = None
try:
    if v is not None:
        os.setxattr(dst, a, v)
    elif a in os.listxattr(dst):
        os.removexattr(dst, a)
except OSError:
    pass
PY
if cat "$N" > "$tmp" && mv -f "$tmp" "$F"; then
  echo "  $F: the stable PortMaster's installer, in place of the beta one of images 1.9-1.12"
else
  rm -f "$tmp"
fi
exit 0
