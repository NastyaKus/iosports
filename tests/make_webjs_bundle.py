#!/usr/bin/env python3
"""Write the bundled page for samples/webjs_demo into <WebJS.app>: a scripted
step-by-step guide (sections, Next/Back handlers, a hit-test helper, a
custom-scheme bridge through iframes, a timer). Usage: make_webjs_bundle.py path/to/WebJS.app"""
import os, sys

app = sys.argv[1]
html = os.path.join(app, "html")
os.makedirs(html, exist_ok=True)
open(os.path.join(html, "guide.css"), "w").write("""
body { margin: 0; font-family: Helvetica; color: white; text-align: center; background: transparent; overflow: hidden; }
section { display: none; position: absolute; left: 0; right: 0; top: 0; bottom: 0; }
section:first-child { display: block; }
.hole { position: absolute; left: 20px; top: 20px; width: 80px; height: 80px; border: 3px solid #fff; border-radius: 40px; }
.shade { position: absolute; left: -500px; top: -500px; width: 120px; height: 120px; border: 500px solid black; opacity: 0.5; z-index: -10; }
.card { width: 260px; margin: 220px auto 0; padding: 10px; background: rgba(0,0,0,0.8); border-radius: 8px; }
.card h1 { font-size: 20px; margin: 4px 0 10px; }
#close { position: absolute; top: 5px; right: 5px; padding: 0 10px; line-height: 30px; border: 1px solid #666; border-radius: 5px; background: #000; z-index: 1; }
#close a { color: #ddd; }
#bar { position: relative; width: 280px; margin: 0 auto; top: 380px; line-height: 32px; background: #222; border-radius: 6px; }
#bar a { color: #aaa; padding: 0 12px; font-weight: bold; }
#back { float: left; } #next { float: right; }
.ready #page { color: #8f8; }
""")
open(os.path.join(html, "guide.js"), "w").write("""
function Solid(x, y) {
  for (var e = document.elementFromPoint(x, y); e; e = e.parentNode)
    if (e.className && e.className.indexOf('see-through') >= 0) return false;
  return true;
}
function Tell(what) {
  var f = document.createElement('iframe');
  f.setAttribute('src', 'command://' + what);
  document.documentElement.appendChild(f);
  f.parentNode.removeChild(f);
}
var current = 0;
function show(i) {
  var s = document.getElementsByTagName('section');
  s[current].style.display = 'none';
  current = i;
  s[current].style.display = 'block';
  document.getElementById('page').innerHTML = (current + 1) + ' of ' + s.length;
  document.getElementById('back').style.visibility = current ? 'visible' : 'hidden';
  if (s[current].getAttribute('data-say')) Tell(s[current].getAttribute('data-say'));
}
window.onload = function () {
  var s = document.getElementsByTagName('section');
  document.getElementById('page').innerHTML = '1 of ' + s.length;
  document.getElementById('back').style.visibility = 'hidden';
  document.getElementById('next').onclick = function () { if (current + 1 < s.length) show(current + 1); else Tell('finished'); return false; };
  document.getElementById('back').addEventListener('click', function (e) { e.preventDefault(); if (current) show(current - 1); });
  setTimeout(function () { document.body.classList.add('ready'); document.title = 'Guide (ready)'; Tell('timer'); }, 50);
};
""")
open(os.path.join(html, "guide.html"), "w").write("""<!DOCTYPE html>
<html><head><meta charset="utf-8"><title>Guide</title>
<script src="guide.js"></script><link rel="stylesheet" href="guide.css"></head>
<body>
  <section>
    <div class="shade"></div><div class="hole see-through"></div>
    <div class="card"><h1>Step one</h1><p>Tap the circle, then Next.</p></div>
  </section>
  <section data-say="second-step">
    <div class="card"><h1>Step two</h1><p>Almost done.</p></div>
  </section>
  <section>
    <div class="card"><h1>Done</h1><p><a href="command://close">Close the guide</a></p></div>
  </section>
  <div id="close"><a href="command://close">Close</a></div>
  <div id="bar"><a href="#" id="back">Back</a><a href="#" id="next">Next</a><div id="page"># of #</div></div>
</body></html>
""")
if not os.path.exists(os.path.join(app, "Info.plist")):
    open(os.path.join(app, "Info.plist"), "w").write("""<?xml version="1.0" encoding="UTF-8"?>
<plist version="1.0"><dict><key>CFBundleExecutable</key><string>WebJS</string>
<key>CFBundleIdentifier</key><string>org.iosrt.webjsdemo</string><key>UILaunchStoryboardName</key><string>LaunchScreen</string></dict></plist>
""")
