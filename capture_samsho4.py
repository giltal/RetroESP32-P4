#!/usr/bin/env python3
"""Capture serial output while loading samsho4."""
import serial, time

s = serial.Serial('COM30', 115200, timeout=2)
print("Capturing samsho4 boot... launch the game now")
start = time.time()
lines = []
while time.time() - start < 60:
    line = s.readline()
    if line:
        text = line.decode('utf-8', 'replace').strip()
        if text:
            lines.append(text)
            print(text, flush=True)
    if len(lines) > 600:
        break
s.close()
print(f"\n=== Captured {len(lines)} lines ===")

# Highlight errors/crashes
for i, l in enumerate(lines):
    ll = l.lower()
    if any(k in ll for k in ['error', 'crash', 'panic', 'abort', 'fault', 'guru', 'watchdog', 'backtrace', 'assert']):
        print(f"  *** [{i}] {l}")
