"""Quick serial capture with device reset — writes to a file."""
import serial, time, sys

PORT = "COM30"
BAUD = 115200
OUTFILE = r"C:\ESPIDFprojects\RetroESP32_P4\boot_log.txt"

try:
    s = serial.Serial(PORT, BAUD, timeout=1)
except Exception as e:
    with open(OUTFILE, "w") as f:
        f.write(f"ERROR: Could not open {PORT}: {e}\n")
    sys.exit(1)

# Reset device via DTR/RTS toggling
s.dtr = False
s.rts = True
time.sleep(0.1)
s.rts = False
s.dtr = True
time.sleep(0.3)

# Flush old data
s.reset_input_buffer()

# Collect lines for ~25 seconds
lines = []
end = time.time() + 25
while time.time() < end:
    line = s.readline().decode("utf-8", "ignore").rstrip()
    if line:
        lines.append(line)
        print(line, flush=True)

s.close()

with open(OUTFILE, "w", encoding="utf-8") as f:
    f.write("\n".join(lines) + "\n")

print(f"\nWrote {len(lines)} lines to {OUTFILE}")
