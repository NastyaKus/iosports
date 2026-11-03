#!/usr/bin/env python3
"""Dump a compiled NIB (NIBArchive format) as an object tree, for debugging the
runtime's NIB loader. Usage: tools/nibdump.py file.nib [--flat]"""
import struct, sys

def varint(b, o):
    r = sh = 0
    while True:
        c = b[o]; o += 1
        r |= (c & 0x7f) << sh; sh += 7
        if c & 0x80:            # high bit marks the LAST byte
            return r, o

def parse(b):
    if b[:10] != b"NIBArchive":
        raise ValueError("not a NIBArchive")
    _, _, no, oo, nk, ok, nv, ov, nc, oc = struct.unpack_from("<10I", b, 10)
    classes = []
    o = oc
    for _ in range(nc):
        ln, o = varint(b, o); ne, o = varint(b, o)
        o += 4 * ne
        classes.append(b[o:o + ln].rstrip(b"\0").decode()); o += ln
    keys = []
    o = ok
    for _ in range(nk):
        ln, o = varint(b, o); keys.append(b[o:o + ln].decode("utf-8", "replace")); o += ln
    values = []
    o = ov
    for _ in range(nv):
        k, o = varint(b, o); t = b[o]; o += 1
        if t == 0: v = struct.unpack_from("<b", b, o)[0]; o += 1
        elif t == 1: v = struct.unpack_from("<h", b, o)[0]; o += 2
        elif t == 2: v = struct.unpack_from("<i", b, o)[0]; o += 4
        elif t == 3: v = struct.unpack_from("<q", b, o)[0]; o += 8
        elif t == 4: v = True
        elif t == 5: v = False
        elif t == 6: v = struct.unpack_from("<f", b, o)[0]; o += 4
        elif t == 7: v = struct.unpack_from("<d", b, o)[0]; o += 8
        elif t == 8:
            ln, o = varint(b, o); v = bytes(b[o:o + ln]); o += ln
        elif t == 9: v = None
        elif t == 10: v = ("@", struct.unpack_from("<I", b, o)[0]); o += 4
        else: raise ValueError("value type %d" % t)
        values.append((keys[k], v))
    objects = []
    o = oo
    for _ in range(no):
        ci, o = varint(b, o); vs, o = varint(b, o); vc, o = varint(b, o)
        objects.append((classes[ci], values[vs:vs + vc]))
    return objects

def fmt(v):
    if isinstance(v, tuple): return "@%d" % v[1]
    if isinstance(v, bytes):
        try:
            s = v.decode()
            if s.isprintable(): return repr(s)
        except UnicodeDecodeError: pass
        return "<%d bytes %s>" % (len(v), v[:16].hex())
    return repr(v)

def main():
    objs = parse(open(sys.argv[1], "rb").read())
    for i, (cls, vals) in enumerate(objs):
        print("#%d %s" % (i, cls))
        for k, v in vals:
            print("    %s = %s" % (k, fmt(v)))

if __name__ == "__main__":
    main()
