#!/usr/bin/env python3
# clear-execstack.py <lib.so>...: clears the executable bit of each ELF file's PT_GNU_STACK header, in place,
# as `patchelf --clear-execstack` or `execstack -c` would. Files that do not ask for an executable stack are
# left untouched; a file with no PT_GNU_STACK header at all is reported, not changed.
#
# The armhf libx264 builds that fetch_compat_libs.sh takes from Debian 10/11 ask for an executable stack
# (assembly without a .note.GNU-stack section, not code run from the stack). Since glibc 2.41 (Debian 13)
# dlopen() refuses such a library ("cannot enable executable stack as shared object requires"), so a
# 32-bit program could not dlopen libavcodec.so.58, which needs libx264.so.160.
import struct
import sys

PT_GNU_STACK = 0x6474E551
PF_X = 1

rc = 0
for path in sys.argv[1:]:
    with open(path, "r+b") as f:
        head = f.read(64)
        if head[:4] != b"\x7fELF" or head[5] != 1:
            print("%s: not a little-endian ELF file" % path)
            rc = 1
            continue
        is64 = head[4] == 2
        if is64:
            phoff, = struct.unpack_from("<Q", head, 0x20)
            phentsize, phnum = struct.unpack_from("<HH", head, 0x36)
            flags_at = 4                    # Elf64_Phdr: p_type, p_flags, ...
        else:
            phoff, = struct.unpack_from("<I", head, 0x1C)
            phentsize, phnum = struct.unpack_from("<HH", head, 0x2A)
            flags_at = 24                   # Elf32_Phdr: p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags
        found = False
        for i in range(phnum):
            f.seek(phoff + i * phentsize)
            ph = f.read(phentsize)
            if struct.unpack_from("<I", ph, 0)[0] != PT_GNU_STACK:
                continue
            found = True
            flags, = struct.unpack_from("<I", ph, flags_at)
            if flags & PF_X:
                f.seek(phoff + i * phentsize + flags_at)
                f.write(struct.pack("<I", flags & ~PF_X))
                print("%s: executable stack cleared" % path)
            else:
                print("%s: no executable stack, unchanged" % path)
        if not found:
            print("%s: no PT_GNU_STACK header, unchanged" % path)
            rc = 1
sys.exit(rc)
