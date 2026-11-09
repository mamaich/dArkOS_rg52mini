#!/usr/bin/env python3
# pm-installer-update.py <Install.PortMaster.sh> <PortMaster.zip>: puts PortMaster.zip into the
# PortMaster installer in place of the one it carries, if that one is another version.
#
# PortMaster's releases carry Install.PortMaster.sh, a Makeself archive: a shell header, then a
# gzipped tar of installer.sh and PortMaster.zip. The release's PortMaster.zip is current, but the
# installer was not rebuilt: the one of 2026.11.02-0433 still installs 2026.06.23-0015. The header
# is kept as it is (upstream's changes: a log, unpacking under /roms/ports rather than a small
# /tmp), with the payload's size and MD5 in it updated; its CRC is set to "none", which Makeself
# accepts (it then checks the MD5 only).
import gzip
import hashlib
import io
import os
import re
import sys
import tarfile
import zipfile

inst_path, zip_path = sys.argv[1], sys.argv[2]
data = open(inst_path, "rb").read()
skip = int(re.search(rb'^skip="(\d+)"$', data[:65536], re.M).group(1))
off = 0
for _ in range(skip):
    off = data.index(b"\n", off) + 1
header, payload = data[:off], data[off:]
size = int(re.search(rb'^filesizes="(\d+)"$', header, re.M).group(1))
if size != len(payload):
    sys.exit("%s: payload is %d bytes, the header says %d" % (inst_path, len(payload), size))


def zip_version(blob):
    return zipfile.ZipFile(io.BytesIO(blob)).read("PortMaster/version").decode().strip()


old = tarfile.open(fileobj=io.BytesIO(gzip.decompress(payload)))
members = old.getmembers()
names = [m.name.lstrip("./") for m in members]
if sorted(names) != ["PortMaster.zip", "installer.sh"]:
    sys.exit("%s: unexpected contents %s" % (inst_path, names))
new_zip = open(zip_path, "rb").read()
have = zip_version(old.extractfile([m for m in members if m.name.endswith("PortMaster.zip")][0]).read())
want = zip_version(new_zip)
if have == want:
    print("Install.PortMaster.sh already installs PortMaster %s" % have)
    sys.exit(0)

out = io.BytesIO()
with tarfile.open(fileobj=out, mode="w", format=tarfile.GNU_FORMAT) as t:
    for m in members:
        if m.name.endswith("PortMaster.zip"):
            m.size = len(new_zip)
            t.addfile(m, io.BytesIO(new_zip))
        else:
            t.addfile(m, old.extractfile(m))
new_payload = gzip.compress(out.getvalue(), compresslevel=9, mtime=0)
md5 = hashlib.md5(new_payload).hexdigest()


def field(h, key, value):
    h2, n = re.subn(rb'^(' + key + rb')="[^"\n]*"$', b'\\1="' + value + b'"', h, count=1, flags=re.M)
    if n != 1:
        sys.exit("%s: no %s in the header" % (inst_path, key.decode()))
    return h2


n = str(len(new_payload)).encode()
header = field(header, b"filesizes", n)
header = field(header, b"totalsize", n)
header = field(header, b"MD5", md5.encode())
header = field(header, b"CRCsum", b"0000000000")
mode = os.stat(inst_path).st_mode & 0o7777
with open(inst_path, "wb") as f:
    f.write(header + new_payload)
os.chmod(inst_path, mode)
print("Install.PortMaster.sh: PortMaster %s -> %s (%d bytes)" % (have, want, len(header) + len(new_payload)))
