#!/usr/bin/env python3
"""Write the bundled pages for samples/web_demo into <Web.app>: two HTML pages,
a stylesheet and a small PNG. Usage: make_web_bundle.py path/to/Web.app"""
import os, struct, sys, zlib

app = sys.argv[1]
html = os.path.join(app, "html")
os.makedirs(html, exist_ok=True)

def png(path, w, h, rgba):
    raw = b"".join(b"\x00" + bytes(rgba) * w for _ in range(h))
    def chunk(t, d): return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    data = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")
    open(path, "wb").write(data)

png(os.path.join(html, "dot.png"), 8, 8, (230, 80, 40, 255))
open(os.path.join(html, "style.css"), "w").write("""/* a comment */
body { color: #333; font-size: 16px; }
h1 { font-size: 30px; text-transform: uppercase; }
.links a { color: rgb(0, 90, 200); }
.legal p { font-size: 8px; }          /* must not shrink paragraphs outside .legal */
.hidden { display: none; }
a.big { font-size: 24px; }
""")
open(os.path.join(html, "index.html"), "w").write("""<!DOCTYPE html>
<html><head><meta charset="utf-8"><title>Web Demo &amp; Co</title>
<link rel="stylesheet" type="text/css" href="style.css"></head>
<body>
<h1>Index</h1>
<div class="links">
<p><a class="big" href="page2.html">Second page</a></p>
<p><a class="big" href="blocked://nope">Blocked link</a></p>
<p><a class="big" href="app://action">App scheme</a></p>
</div>
<p>Plain paragraph with an image <img src="dot.png" width="16" height="16"> and entities: caf&eacute; &rarr; &#x2713;</p>
<ul class="hidden"><li>never shown</li></ul>
<div class="legal"><p>tiny legal text</p></div>
</body></html>
""")
open(os.path.join(html, "page2.html"), "w").write("""<html><head><title>Second</title><link rel="stylesheet" href="style.css"></head>
<body><h1>Page two</h1><ul><li>first item</li><li>second item</li></ul><p><a href="index.html#top">back to index</a></p></body></html>
""")
if not os.path.exists(os.path.join(app, "Info.plist")):   # keep a plist the caller already wrote
    open(os.path.join(app, "Info.plist"), "w").write("""<?xml version="1.0" encoding="UTF-8"?>
<plist version="1.0"><dict><key>CFBundleExecutable</key><string>Web</string>
<key>CFBundleIdentifier</key><string>org.iosrt.webdemo</string><key>UILaunchStoryboardName</key><string>LaunchScreen</string></dict></plist>
""")
