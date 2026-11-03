#!/usr/bin/env python3
"""Write Archive.app for samples/archive_demo: Info.plist and fixture.archive,
an NSKeyedArchiver archive in Apple's binary format built by hand (plistlib
has UID support), with a $classes fallback chain and a renamed class.
Usage: make_archive_fixture.py path/to/Archive.app"""
import os, plistlib, sys
from plistlib import UID

app = sys.argv[1]
os.makedirs(app, exist_ok=True)
objects = ["$null"]
def add(o):
    objects.append(o)
    return UID(len(objects) - 1)
def cls(*names):
    return add({"$classname": names[0], "$classes": list(names)})

title = add("Death Worm & Co")
difficulty = add("hard")
settings_cls = cls("GameSettingsV2", "GameSettings", "NSObject")   # V2 does not exist in the app
settings = add({"$class": settings_cls, "volume": 7, "difficulty": difficulty})
legacy_cls = cls("LegacyThing", "NSObject")                        # mapped with +setClass:forClassName:
tag = add("renamed")
legacy = add({"$class": legacy_cls, "tag": tag})
arr_cls = cls("NSArray", "NSObject")
nums = add({"$class": arr_cls, "NS.objects": [add(1), add(2), add(3)]})
dict_cls = cls("NSDictionary", "NSObject")
keys = [add("title"), add("settings"), add("legacy"), add("numbers"), add("flag")]
vals = [title, settings, legacy, nums, add(True)]
root = add({"$class": dict_cls, "NS.keys": keys, "NS.objects": vals})
archive = {"$archiver": "NSKeyedArchiver", "$version": 100000, "$top": {"root": root}, "$objects": objects}
with open(os.path.join(app, "fixture.archive"), "wb") as f:
    plistlib.dump(archive, f, fmt=plistlib.FMT_BINARY)
with open(os.path.join(app, "Info.plist"), "w") as f:
    f.write("""<?xml version="1.0" encoding="UTF-8"?>
<plist version="1.0"><dict><key>CFBundleExecutable</key><string>Archive</string>
<key>CFBundleIdentifier</key><string>org.iosrt.archivedemo</string></dict></plist>
""")
