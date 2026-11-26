#!/usr/bin/env python3
# pm-installer-version.py <Install.PortMaster.sh>: prints the version of the PortMaster that the
# installer installs, after checking the installer is whole. The build compares it with the stable
# channel's version in PortMaster's version.json, so that the image never ships another channel.
#
# PortMaster's releases carry Install.PortMaster.sh, a Makeself archive: a shell header, then a
# gzipped tar of installer.sh and PortMaster.zip. PortMaster has three channels (stable, beta,
# alpha). The release GitHub calls "latest" is a beta one, and its PortMaster.zip is the beta;
# its Install.PortMaster.sh is the stable one's, byte for byte, and installs the stable version on
# purpose. Images 1.9 to 1.12 put the latest release's PortMaster.zip into the installer (this file
# was pm-installer-update.py then) and so installed the beta channel.
import gzip
import hashlib
import io
import re
import sys
import tarfile
import zipfile

inst_path = sys.argv[1]
data = open(inst_path, "rb").read()
skip = int(re.search(rb'^skip="(\d+)"$', data[:65536], re.M).group(1))
off = 0
for _ in range(skip):
    off = data.index(b"\n", off) + 1
header, payload = data[:off], data[off:]
size = int(re.search(rb'^filesizes="(\d+)"$', header, re.M).group(1))
if size != len(payload):
    sys.exit("%s: payload is %d bytes, the header says %d" % (inst_path, len(payload), size))
md5 = re.search(rb'^MD5="([0-9a-f]{32})"$', header, re.M)
if md5 and md5.group(1).decode() != hashlib.md5(payload).hexdigest():
    sys.exit("%s: the payload's MD5 is not the header's" % inst_path)

tar = tarfile.open(fileobj=io.BytesIO(gzip.decompress(payload)))
members = tar.getmembers()
names = [m.name.lstrip("./") for m in members]
if sorted(names) != ["PortMaster.zip", "installer.sh"]:
    sys.exit("%s: unexpected contents %s" % (inst_path, names))
blob = tar.extractfile([m for m in members if m.name.endswith("PortMaster.zip")][0]).read()
print(zipfile.ZipFile(io.BytesIO(blob)).read("PortMaster/version").decode().strip())
