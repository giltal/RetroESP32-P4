"""Passive serial capture — no reset. Just records for 60 seconds."""
import serial, time, sys

PORT = "COM30"
BAUD = 115200
OUTFILE = r"C:\ESPIDFprojects\RetroESP32_P4\crash_log.txt"

try:
    s = serial.Serial(PORT, BAUD, timeout=1)
except Exception as e:
    with open(OUTFILE, "w") as f:
        f.write(f"ERROR: Could not open {PORT}: {e}\n")
    sys.exit(1)

s.reset_input_buffer()
lines = []
end = time.time() + 60
print("Capturing serial for 60 seconds (no reset). Launch the app now...")
while time.time() < end:
    line = s.readline().decode("utf-8", "ignore").rstrip()
    if line:
        lines.append(line)
        print(line, flush=True)

s.close()
with open(OUTFILE, "w", encoding="utf-8") as f:
    f.write("\n".join(lines) + "\n")
print(f"\nWrote {len(lines)} lines to {OUTFILE}")
