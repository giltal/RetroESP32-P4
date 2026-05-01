#!/usr/bin/env python3
"""Capture FPS output from serial for ~15 seconds."""
import serial, time

s = serial.Serial('COM30', 115200, timeout=1)
print("Capturing FPS for 15 seconds... play a game now")
start = time.time()
fps_lines = []
while time.time() - start < 15:
    line = s.readline()
    if line:
        text = line.decode('utf-8', 'replace').strip()
        if 'FPS' in text:
            fps_lines.append(text)
            print(text)
s.close()

if fps_lines:
    # Parse rendered FPS values
    rendered = []
    for l in fps_lines:
        try:
            # Format: "FPS: X (rendered: Y, skipped: Z)"
            parts = l.split('rendered: ')[1].split(',')[0]
            rendered.append(int(parts))
        except:
            pass
    if rendered:
        print(f"\n--- Summary (raster mode) ---")
        print(f"Samples: {len(rendered)}")
        print(f"Avg rendered FPS: {sum(rendered)/len(rendered):.1f}")
        print(f"Min: {min(rendered)}, Max: {max(rendered)}")
else:
    print("No FPS lines captured. Make sure a game is running.")
