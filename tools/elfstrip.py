#!/usr/bin/env python3
# The shipped binary from the debug link itself (tools/build.sh): what the TV loads (its program segments) kept byte
# for byte, the debug sections and the section table after them left out. Crash addresses then match the debug
# build exactly (two links, one with --strip-all, came out a few bytes apart). Usage: elfstrip.py in out
import hashlib, struct, sys
d = open(sys.argv[1], 'rb').read()
if d[:4] != b'\x7fELF' or d[4] != 1 or d[5] != 1: sys.exit('elfstrip: a 32-bit little-endian ELF only')
phoff, = struct.unpack_from('<I', d, 0x1c); phentsize, phnum = struct.unpack_from('<HH', d, 0x2a)
end = phoff + phentsize * phnum
for i in range(phnum):
    p_type, p_offset, _, _, p_filesz = struct.unpack_from('<IIIII', d, phoff + i * phentsize)
    if p_type != 0: end = max(end, p_offset + p_filesz)
d = bytearray(d)
i = d.find(b'SBBUILDID:' + b'.' * 32) # the build's id: a hash of this link, written into it (and so into the debug build too)
if i >= 0:
    d[i + 10:i + 42] = hashlib.sha256(bytes(d)).hexdigest()[:32].encode(); open(sys.argv[1], 'wb').write(d)
o = bytearray(d[:end])
struct.pack_into('<I', o, 0x20, 0); struct.pack_into('<HHH', o, 0x2e, 0, 0, 0) # no section table
open(sys.argv[2], 'wb').write(o)
