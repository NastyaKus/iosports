#!/usr/bin/env python3
"""Verify a WAV produced by samples/audio_sine: ~1.5 s of a 440 Hz sine at amplitude 0.5."""
import sys, wave, struct
w = wave.open(sys.argv[1]); n, ch, rate = w.getnframes(), w.getnchannels(), w.getframerate()
left = struct.unpack("<%dh" % (n * ch), w.readframes(n))[0::ch]
secs = n / rate
zc = sum(1 for a, b in zip(left, left[1:]) if a < 0 <= b)
freq, peak = zc / secs, max(left) / 32767
ok = 1.2 <= secs <= 2.5 and abs(freq - 440) < 3 and abs(peak - 0.5) < 0.02
print(f"wav: {secs:.2f}s {rate}Hz ch={ch} freq={freq:.1f}Hz peak={peak:.2f} -> {'OK' if ok else 'FAIL'}")
sys.exit(0 if ok else 1)
