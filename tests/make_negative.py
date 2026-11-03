#!/usr/bin/env python3
"""Craft malformed/unsupported Mach-O inputs from a known-good one."""
import struct, sys
src, outdir = sys.argv[1], sys.argv[2]
data = bytearray(open(src, "rb").read())
# 1. FairPlay-encrypted: set cryptid=1 in LC_ENCRYPTION_INFO_64
enc = bytearray(data); ncmds = struct.unpack_from("<I", enc, 16)[0]; off = 32
for _ in range(ncmds):
    cmd, size = struct.unpack_from("<II", enc, off)
    if cmd == 0x2C: struct.pack_into("<I", enc, off + 16, 1)
    off += size
open(f"{outdir}/encrypted", "wb").write(enc)
# 2. x86_64 thin binary (cputype patched)
x = bytearray(data); struct.pack_into("<I", x, 4, 0x01000007); open(f"{outdir}/x86_64", "wb").write(x)
# 3. fat binary containing only an armv7 slice
thin = bytearray(data); struct.pack_into("<I", thin, 0, 0xfeedface); struct.pack_into("<I", thin, 4, 12)
fat = struct.pack(">II", 0xcafebabe, 1) + struct.pack(">IIIII", 12, 9, 4096, len(thin), 12)
fat = fat + b"\0" * (4096 - len(fat)) + thin
open(f"{outdir}/armv7_only", "wb").write(fat)
# 4. not a Mach-O at all
open(f"{outdir}/garbage", "wb").write(b"PK\x03\x04 definitely not a mach-o file")
