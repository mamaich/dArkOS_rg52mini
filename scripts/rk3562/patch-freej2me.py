#!/usr/bin/env python3
"""Run both J2ME cores on the OpenJDK 21 that the image already has.

usage: patch-freej2me.py PATH-TO-freej2me.sh

The libretro core "freej2me" runs freej2me-lr.jar, which is compiled for Java
21. Upstream's /usr/local/bin/freej2me.sh downloads Azul Zulu JDK 11 (197 MB,
needs Wi-Fi) and runs the core on that, which dies with
UnsupportedClassVersionError. This patch uses
/usr/lib/jvm/java-21-openjdk-arm64 instead, so nothing is downloaded, and it
starts Java with -Dfile.encoding=ISO_8859_1: under Java 21 the default is
UTF-8, which crashes some games (Prince of Persia: Warrior Within,
ArrayIndexOutOfBounds 65533). Since 2026-11-21 the "freej2me-plus" core runs
on it too (its jar is built for Java 8, build_retroarch.sh; rg503's core of
2026-11-17 played a test game there at 60 fps), so neither core downloads
anything.

finishing_touches-rk3562.sh must run this after it copies scripts/freej2me.sh:
from 1.7 to 1.11 it ran before, and the copy put the stock script back.

If the script is not the stock one, nothing is changed and the build goes on.
The script falls back to the old behaviour when the OpenJDK 21 is not there.
"""
import sys

path = sys.argv[1]
s = open(path).read()
if 'USE_SYS21' in s:
    print("freej2me.sh: already patched")
    sys.exit(0)

EDITS = [
    ('JDKDOWNLOAD="zulu11.48.21-ca-jdk11.0.11"',
     '''JDKDOWNLOAD="zulu11.48.21-ca-jdk11.0.11"

# The freej2me core (freej2me-lr.jar) is compiled for Java 21 and fails on the
# downloaded JDK 11 (UnsupportedClassVersionError). The image has OpenJDK 21: use
# it for both cores (freej2me-plus's jar is built for Java 8), no download.
SYSJDK21=/usr/lib/jvm/java-21-openjdk-arm64
if { [ "$2" = "freej2me" ] || [ "$2" = "freej2me-plus" ]; } && [ -x "$SYSJDK21/bin/java" ]; then USE_SYS21=1; fi'''),
    ('if [[ $(ls -l /${directory}/j2me/jdk/bin/java > /dev/null 2>&1 && echo $?) != 0 ]]; then',
     'if [ -z "$USE_SYS21" ] && [[ $(ls -l /${directory}/j2me/jdk/bin/java > /dev/null 2>&1 && echo $?) != 0 ]]; then'),
    ('export PATH=/${directory}/j2me/jdk/bin:$PATH\nexport JAVA_HOME=/${directory}/j2me/jdk',
     'export PATH=/${directory}/j2me/jdk/bin:$PATH\nexport JAVA_HOME=/${directory}/j2me/jdk\n'
     'if [ -n "$USE_SYS21" ]; then export JAVA_HOME=$SYSJDK21; export PATH=$SYSJDK21/bin:$PATH; '
     'export JAVA_TOOL_OPTIONS="-Dfile.encoding=ISO_8859_1"; fi'),
]

for old, _ in EDITS:
    if s.count(old) != 1:
        print("freej2me.sh is not the stock one (expected text missing: %r): left as it is" % old[:50])
        sys.exit(0)
for old, new in EDITS:
    s = s.replace(old, new, 1)
open(path, 'w').write(s)
print("freej2me.sh: both J2ME cores now run on the system OpenJDK 21")
