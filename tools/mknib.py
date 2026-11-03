#!/usr/bin/env python3
"""Write compiled NIBs (NIBArchive format) from Python descriptions, so the
runtime's NIB loader can be tested without Interface Builder files.

    from mknib import Nib
    nib = Nib()
    owner = nib.proxy("IBFilesOwner")
    view = nib.view("UIView", frame=(0, 0, 320, 480), bg=(1, 1, 1, 1))
    nib.outlet(owner, "view", view)
    nib.write("Main.nib", top=[owner, view])

Usage as a script builds the nib used by samples/nib_demo: mknib.py out.nib;
mknib.py --main-window MainWindow.nib builds the keyed nib of samples/keyed_nib_demo;
mknib.py --storyboard Main.storyboardc builds the storyboard of samples/storyboard_demo.
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

    # ------------------------------------------------ keyed (old ibtool) format
    def write_keyed(self, path, top, fmt="binary"):
        """Same object graph as write(), as an NSKeyedArchiver archive: the
        UINib* keys on $top, NSString/NSNumber values inline, arrays and
        dictionaries as NS.objects/NS.keys, geometry blobs as "{x, y}" strings."""
        import plistlib
        from plistlib import UID
        objects = ["$null"]
        uid_of, class_uids = {}, {}
        def cls_uid(name):
            if name not in class_uids:
                objects.append({"$classname": name, "$classes": [name, "NSObject"]})
                class_uids[name] = UID(len(objects) - 1)
            return class_uids[name]
        def geometry(b):
            n = (len(b) - 1) // 8
            v = struct.unpack("<%dd" % n, b[1:1 + 8 * n])
            f = lambda x: ("%g" % x)
            return "{%s, %s}" % (f(v[0]), f(v[1])) if n == 2 else "{{%s, %s}, {%s, %s}}" % tuple(map(f, v))
        def enc(idx):
            if idx in uid_of: return uid_of[idx]
            cls, vals = self.objects[idx]
            if cls == "NSString":
                objects.append(dict(vals)["NS.bytes"].decode()); uid_of[idx] = UID(len(objects) - 1); return uid_of[idx]
            if cls == "NSNumber":
                objects.append(dict(vals)["NS.intval"]); uid_of[idx] = UID(len(objects) - 1); return uid_of[idx]
            d = {}
            objects.append(d); uid_of[idx] = UID(len(objects) - 1)
            items = [v for k, v in vals if k == "UINibEncoderEmptyKey"]
            if cls in ("NSArray", "NSMutableArray"):
                d["NS.objects"] = [conv(v) for v in items]
            elif cls in ("NSDictionary", "NSMutableDictionary"):
                d["NS.keys"] = [conv(v) for v in items[0::2]]; d["NS.objects"] = [conv(v) for v in items[1::2]]
            else:
                for k, v in vals:
                    if k == "NSInlinedValue": continue
                    d[k] = geometry(v) if isinstance(v, bytes) and v[:1] == b"\x07" else conv(v)
            d["$class"] = cls_uid(cls)
            return uid_of[idx]
        def conv(v):
            if isinstance(v, Ref): return enc(v.idx)
            if isinstance(v, (bool, int, float, bytes)): return v
            return "$null" if v is None else v
        root_vals = dict(self.objects[0][1]) if self.objects[0][1] else {}
        self.objects[0] = ("NSObject", [])
        all_objs = [Ref(i) for i in range(1, len(self.objects))]
        topd = {"UINibTopLevelObjectsKey": enc(self.array(top, mutable=False).idx),
                "UINibObjectsKey": enc(self.array(all_objs, mutable=False).idx),
                "UINibConnectionsKey": enc(self.array(self.connections, mutable=False).idx),
                "UINibVisibleWindowsKey": enc(self.array([], mutable=False).idx)}
        archive = {"$archiver": "NSKeyedArchiver", "$version": 100000, "$top": topd, "$objects": objects}
        with open(path, "wb") as f:
            plistlib.dump(archive, f, fmt=plistlib.FMT_BINARY if fmt == "binary" else plistlib.FMT_XML)

def main_window_nib(path):
    """MainWindow.nib for samples/keyed_nib_demo: File's Owner is the
    UIApplication, its delegate outlet points at a controller object from
    the nib, which owns the window, a label and a button."""
    nib = Nib()
    owner = nib.proxy("IBFilesOwner")
    nib.proxy("IBFirstResponder")
    ctl = nib.obj("KeyedNibController", [])
    label = nib.view("UILabel", (20, 100, 280, 40), extra=[("UIText", nib.string("From a keyed nib")), ("UITextColor", nib.color(1, 1, 1)),
                                                        ("UIFont", nib.font(20, bold=True)), ("UITextAlignment", 1)])
    content = nib.obj("UIButtonContent", [("UITitle", nib.string("Press")), ("UITitleColor", nib.color(1, 1, 1))])
    button = nib.view("UIButton", (110, 200, 100, 44), bg=(0.3, 0.6, 0.2),
                      extra=[("UIButtonType", 0), ("UIButtonStatefulContent", nib.dictionary([(nib.number(0), content)]))])
    badge = nib.view("UIView", (140, 300, 40, 40), custom_class="KeyedBadge", bg=(0.9, 0.2, 0.2))
    window = nib.view("UIWindow", (0, 0, 320, 480), bg=(0.15, 0.1, 0.3), subviews=[label, button, badge])
    nib.outlet(owner, "delegate", ctl)
    nib.outlet(ctl, "window", window)
    nib.outlet(ctl, "label", label)
    nib.action(button, "pressed:", ctl)
    nib.write_keyed(path, top=[owner, ctl, window])

def storyboard_demo(path):
    """Main.storyboardc for samples/storyboard_demo, laid out like ibtool's output:
    scene nibs whose File's Owner receives the controller through
    "sceneViewController", the storyboard as the "UIStoryboardPlaceholder"
    external object, lazily loaded view nibs (UINibName) with external objects
    for segue templates, a show segue triggered by a button and an embed segue
    into a container view."""
    import os, plistlib
    os.makedirs(path, exist_ok=True)
    def scene(nibname, build):
        nib = Nib()
        owner = nib.proxy("IBFilesOwner")
        sbp = nib.proxy("UIStoryboardPlaceholder")
        vc = build(nib, sbp)
        nib.outlet(owner, "sceneViewController", vc)
        nib.write(os.path.join(path, nibname + ".nib"), top=[owner, vc])
    def vc_obj(nib, cls, base, extra=()):
        return nib.obj("UIClassSwapper", list(extra) + [("UIClassName", nib.string(cls)), ("UIOriginalClassName", nib.string(base))])
    # navigation controller scene with the list controller as its root
    def nav_scene(nib, sbp):
        push = nib.obj("UIStoryboardShowSegueTemplate", [("UIIdentifier", nib.string("showDetail")),
                       ("UIDestinationViewControllerIdentifier", nib.string("DetailController"))])
        embed = nib.obj("UIStoryboardEmbedSegueTemplate", [("UIIdentifier", nib.string("embedBadge")),
                        ("UIDestinationViewControllerIdentifier", nib.string("BadgeController")), ("UIPerformOnViewLoad", True), ("UIContainerView", None)])
        item = nib.obj("UINavigationItem", [("UITitle", nib.string("List"))])
        lst = vc_obj(nib, "ListController", "UIViewController", [
            ("UINibName", nib.string("list-view")), ("UINavigationItem", item),
            ("UIStoryboardSegueTemplates", nib.array([push, embed], mutable=False)),
            ("UIExternalObjectsTableForViewLoading", nib.dictionary([(nib.string("UpstreamPlaceholder-push"), push), (nib.string("UpstreamPlaceholder-embed"), embed)]))])
        nav = nib.obj("UINavigationController", [("UIViewControllers", nib.array([lst])), ("UINavigationBarHidden", False)])
        nib.outlet(nav, "storyboard", sbp)
        nib.outlet(lst, "storyboard", sbp)
        return nav
    scene("UINavigationController-nav", nav_scene)
    # the list controller's view: a button triggering the show segue, a container for the embed
    nib = Nib()
    owner = nib.proxy("IBFilesOwner")
    pushp, embedp = nib.proxy("UpstreamPlaceholder-push"), nib.proxy("UpstreamPlaceholder-embed")
    content = nib.obj("UIButtonContent", [("UITitle", nib.string("Open detail")), ("UITitleColor", nib.color(1, 1, 1))])
    button = nib.view("UIButton", (20, 200, 200, 44), bg=(0.2, 0.4, 0.9), extra=[("UIButtonType", 0), ("UIButtonStatefulContent", nib.dictionary([(nib.number(0), content)]))])
    container = nib.view("UIView", (20, 300, 120, 80), bg=(0.9, 0.9, 0.9))
    root = nib.view("UIView", (0, 0, 375, 667), bg=(1, 1, 1), mask=18, subviews=[button, container])
    nib.outlet(owner, "view", root)
    nib.action(button, "perform:", pushp)
    nib.outlet(embedp, "containerView", container)
    nib.write(os.path.join(path, "list-view.nib"), top=[owner, root])
    # detail scene and its view
    def detail_scene(nib, sbp):
        vc = vc_obj(nib, "DetailController", "UIViewController", [("UINibName", nib.string("detail-view")), ("UIStoryboardIdentifier", nib.string("DetailController"))])
        nib.outlet(vc, "storyboard", sbp)
        return vc
    scene("DetailController", detail_scene)
    nib = Nib()
    owner = nib.proxy("IBFilesOwner")
    label = nib.view("UILabel", (20, 100, 300, 40), extra=[("UIText", nib.string("Detail from the storyboard")), ("UITextColor", nib.color(0, 0, 0))])
    root = nib.view("UIView", (0, 0, 375, 667), bg=(0.95, 0.95, 0.8), mask=18, subviews=[label])
    nib.outlet(owner, "view", root)
    nib.outlet(owner, "label", label)
    nib.write(os.path.join(path, "detail-view.nib"), top=[owner, root])
    # the embedded badge
    def badge_scene(nib, sbp):
        vc = vc_obj(nib, "BadgeController", "UIViewController", [("UINibName", nib.string("badge-view"))])
        nib.outlet(vc, "storyboard", sbp)
        return vc
    scene("BadgeController", badge_scene)
    nib = Nib()
    owner = nib.proxy("IBFilesOwner")
    root = nib.view("UIView", (0, 0, 120, 80), bg=(0.9, 0.3, 0.3), mask=18)
    nib.outlet(owner, "view", root)
    nib.write(os.path.join(path, "badge-view.nib"), top=[owner, root])
    with open(os.path.join(path, "Info.plist"), "wb") as f:
        plistlib.dump({"UIStoryboardDesignatedEntryPointIdentifier": "UINavigationController-nav", "UIStoryboardVersion": 1,
                       "UIViewControllerIdentifiersToNibNames": {"UINavigationController-nav": "UINavigationController-nav",
                                                                 "DetailController": "DetailController", "BadgeController": "BadgeController"}},
                      f, fmt=plistlib.FMT_BINARY)

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
    if len(sys.argv) > 2 and sys.argv[1] == "--main-window":
        main_window_nib(sys.argv[2])
    elif len(sys.argv) > 2 and sys.argv[1] == "--storyboard":
        storyboard_demo(sys.argv[2])
    else:
        demo_nib(sys.argv[1] if len(sys.argv) > 1 else "Main.nib")
