#!/usr/bin/env python3
"""Run the freej2me core on the OpenJDK 21 that the image already has.

usage: patch-freej2me.py PATH-TO-freej2me.sh

The libretro core "freej2me" runs freej2me-lr.jar, which is compiled for Java
21. Upstream's /usr/local/bin/freej2me.sh downloads Azul Zulu JDK 11 (197 MB,
needs Wi-Fi) and runs the core on that, which dies with
UnsupportedClassVersionError. For the "freej2me" core this patch uses
/usr/lib/jvm/java-21-openjdk-arm64 instead, so nothing is downloaded, and it
starts Java with -Dfile.encoding=ISO_8859_1: under Java 21 the default is
UTF-8, which crashes some games (Prince of Persia: Warrior Within,
ArrayIndexOutOfBounds 65533). The "freej2me-plus" core keeps the JDK 11 path.

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
# it for that core, no download.
SYSJDK21=/usr/lib/jvm/java-21-openjdk-arm64
if [ "$2" = "freej2me" ] && [ -x "$SYSJDK21/bin/java" ]; then USE_SYS21=1; fi'''),
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
print("freej2me.sh: the freej2me core now runs on the system OpenJDK 21")
