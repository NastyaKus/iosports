#!/usr/bin/env python3
"""Generate the mini-SDK's text-based stubs (.tbd) from runtime/src/exports/*.def,
so test apps link against exactly the API surface the runtime implements."""
import re, sys, pathlib
defdir = pathlib.Path(sys.argv[1]); sdk = pathlib.Path(sys.argv[2])
INSTALL = {
    "libSystem": "/usr/lib/libSystem.B.dylib",
    "libobjc": "/usr/lib/libobjc.A.dylib",
}
def install_name(lib):
    return INSTALL.get(lib, f"/System/Library/Frameworks/{lib}.framework/{lib}")
def tbd_path(lib):
    if lib.startswith("lib"):
        return sdk / "usr/lib" / (lib + ".tbd")
    return sdk / f"System/Library/Frameworks/{lib}.framework/{lib}.tbd"
libs = {}
for f in sorted(defdir.glob("*.def")):
    for line in f.read_text().splitlines():
        m = re.match(r"\s*SYM\(\s*(\w+)\s*,\s*([\w$]+)\s*,", line)
        if m:
            libs.setdefault(m.group(1), {"syms": set(), "classes": set()})["syms"].add(m.group(2)); continue
        m = re.match(r"\s*CLASS\(\s*(\w+)\s*,\s*(\w+)\s*\)", line)
        if m:
            libs.setdefault(m.group(1), {"syms": set(), "classes": set()})["classes"].add(m.group(2))
libs.setdefault("libSystem", {"syms": set(), "classes": set()})
for lib, d in libs.items():
    p = tbd_path(lib); p.parent.mkdir(parents=True, exist_ok=True)
    out = ["--- !tapi-tbd", "tbd-version: 4", "targets: [ arm64-ios ]", f"install-name: '{install_name(lib)}'",
           "current-version: 1", "exports:", "  - targets: [ arm64-ios ]"]
    if d["syms"]:
        out.append("    symbols: [ " + ", ".join(f"'{s}'" for s in sorted(d["syms"])) + " ]")
    if d["classes"]:
        out.append("    objc-classes: [ " + ", ".join(sorted(d["classes"])) + " ]")
    out.append("...")
    p.write_text("\n".join(out) + "\n")
print(f"wrote {len(libs)} tbd stubs")
