#!/usr/bin/env python3
"""Write compiled NIBs (NIBArchive format) from Python descriptions, so the
runtime's NIB loader can be tested without Interface Builder files.

    from mknib import Nib
    nib = Nib()
    owner = nib.proxy("IBFilesOwner")
    view = nib.view("UIView", frame=(0, 0, 320, 480), bg=(1, 1, 1, 1))
    nib.outlet(owner, "view", view)
    nib.write("Main.nib", top=[owner, view])

Usage as a script builds the nib used by samples/nib_demo: mknib.py out.nib
"""
import struct, sys

def varint(n):
    out = bytearray()
    while True:
        b = n & 0x7f; n >>= 7
        if n == 0:
            out.append(b | 0x80)       # high bit marks the last byte
            return bytes(out)
        out.append(b)

class Ref:
    def __init__(self, idx): self.idx = idx

class Nib:
    def __init__(self):
        self.objects = []                       # (class, [(key, value)])
        self.root = self.obj("NSObject", [])    # object 0, filled in by write()
        self.connections = []

    def obj(self, cls, values):
        self.objects.append((cls, list(values)))
        return Ref(len(self.objects) - 1)

    # --- value helpers
    def string(self, s): return self.obj("NSString", [("NS.bytes", s.encode())])
    def number(self, n): return self.obj("NSNumber", [("NS.intval", int(n))])
    def array(self, items, mutable=True):
        return self.obj("NSMutableArray" if mutable else "NSArray", [("NSInlinedValue", False)] + [("UINibEncoderEmptyKey", i) for i in items])
    def dictionary(self, pairs):
        vals = [("NSInlinedValue", False)]
        for k, v in pairs: vals += [("UINibEncoderEmptyKey", k), ("UINibEncoderEmptyKey", v)]
        return self.obj("NSMutableDictionary", vals)
    def color(self, r, g, b, a=1.0):
        return self.obj("UIColor", [("UIColorComponentCount", 4), ("UIRed", float(r)), ("UIGreen", float(g)), ("UIBlue", float(b)), ("UIAlpha", float(a))])
    def font(self, size, bold=False):
        return self.obj("UIFont", [("UIFontName", self.string(".SFUI-Semibold" if bold else ".SFUI-Regular")), ("UIFontPointSize", float(size)),
                                   ("UIFontTraits", 2 if bold else 0), ("UISystemFont", True)])
    def proxy(self, ident): return self.obj("UIProxyObject", [("UIProxiedObjectIdentifier", self.string(ident))])

    @staticmethod
    def geometry(frame):
        x, y, w, h = frame
        return [("UIBounds", b"\x07" + struct.pack("<4d", 0, 0, w, h)), ("UICenter", b"\x07" + struct.pack("<2d", x + w / 2, y + h / 2))]

    def view(self, cls, frame, bg=None, subviews=(), mask=0, extra=(), custom_class=None):
        vals = self.geometry(frame) + [("UIAutoresizingMask", mask), ("UIAutoresizeSubviews", False)]
        if bg is not None: vals.append(("UIBackgroundColor", self.color(*bg)))
        if subviews: vals.append(("UISubviews", self.array(list(subviews))))
        vals += list(extra)
        if custom_class:
            vals += [("UIClassName", self.string(custom_class)), ("UIOriginalClassName", self.string(cls))]
            return self.obj("UIClassSwapper", vals)
        return self.obj(cls, vals)

    def outlet(self, source, label, dest):
        self.connections.append(self.obj("UIRuntimeOutletConnection", [("UILabel", self.string(label)), ("UISource", source), ("UIDestination", dest)]))
    def action(self, source, selector, target, mask=64):
        self.connections.append(self.obj("UIRuntimeEventConnection", [("UILabel", self.string(selector)), ("UISource", source), ("UIDestination", target), ("UIEventMask", mask)]))

    def write(self, path, top):
        all_objs = [Ref(i) for i in range(1, len(self.objects))]
        root_vals = [("UINibTopLevelObjectsKey", self.array(top, mutable=False)),
                     ("UINibObjectsKey", self.array(all_objs, mutable=False)),
                     ("UINibConnectionsKey", self.array(self.connections, mutable=False)),
                     ("UINibVisibleWindowsKey", self.array([], mutable=False))]
        self.objects[0] = ("NSObject", root_vals)
        keys, classes, values, objs = [], [], [], []
        def key_index(k):
            if k not in keys: keys.append(k)
            return keys.index(k)
        def class_index(c):
            if c not in classes: classes.append(c)
            return classes.index(c)
        for cls, vals in self.objects:
            start = len(values)
            for k, v in vals:
                ki = key_index(k)
                if isinstance(v, Ref): values.append(varint(ki) + bytes([10]) + struct.pack("<I", v.idx))
                elif isinstance(v, bool): values.append(varint(ki) + bytes([4 if v else 5]))
                elif isinstance(v, int):
                    if -128 <= v < 128: values.append(varint(ki) + bytes([0]) + struct.pack("<b", v))
                    elif -2**31 <= v < 2**31: values.append(varint(ki) + bytes([2]) + struct.pack("<i", v))
                    else: values.append(varint(ki) + bytes([3]) + struct.pack("<q", v))
                elif isinstance(v, float): values.append(varint(ki) + bytes([7]) + struct.pack("<d", v))
                elif isinstance(v, bytes): values.append(varint(ki) + bytes([8]) + varint(len(v)) + v)
                elif v is None: values.append(varint(ki) + bytes([9]))
                else: raise TypeError(v)
            objs.append(varint(class_index(cls)) + varint(start) + varint(len(vals)))
        sec_objs = b"".join(objs)
        sec_keys = b"".join(varint(len(k.encode())) + k.encode() for k in keys)
        sec_vals = b"".join(values)
        sec_cls = b"".join(varint(len(c) + 1) + varint(0) + c.encode() + b"\0" for c in classes)
        hdr_len = 10 + 4 * 10
        o_objs = hdr_len; o_keys = o_objs + len(sec_objs); o_vals = o_keys + len(sec_keys); o_cls = o_vals + len(sec_vals)
        header = b"NIBArchive" + struct.pack("<10I", 1, 9, len(objs), o_objs, len(keys), o_keys, len(values), o_vals, len(classes), o_cls)
        with open(path, "wb") as f:
            f.write(header + sec_objs + sec_keys + sec_vals + sec_cls)

def demo_nib(path):
    nib = Nib()
    owner = nib.proxy("IBFilesOwner")
    nib.proxy("IBFirstResponder")
    title = nib.view("UILabel", (20, 80, 280, 40), extra=[("UIText", nib.string("Loaded from a NIB")), ("UITextColor", nib.color(1, 1, 1)),
                                                        ("UIFont", nib.font(22, bold=True)), ("UITextAlignment", 1)])
    content = nib.obj("UIButtonContent", [("UITitle", nib.string("Tap me")), ("UITitleColor", nib.color(1, 1, 1))])
    button = nib.view("UIButton", (80, 160, 160, 50), bg=(0.2, 0.5, 1),
                      extra=[("UIButtonType", 0), ("UIButtonStatefulContent", nib.dictionary([(nib.number(0), content)]))])
    badge = nib.view("UIView", (130, 240, 60, 60), custom_class="BadgeView", extra=[("UITag", 7)])
    root = nib.view("UIView", (0, 0, 320, 480), bg=(0.1, 0.12, 0.2), mask=18, subviews=[title, button, badge])
    nib.outlet(owner, "view", root)
    nib.outlet(owner, "titleLabel", title)
    nib.outlet(owner, "badge", badge)
    nib.action(button, "tapped:", owner)
    nib.write(path, top=[owner, root])

if __name__ == "__main__":
    demo_nib(sys.argv[1] if len(sys.argv) > 1 else "Main.nib")
