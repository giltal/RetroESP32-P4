"""Flash firmware and immediately capture serial output for kof98 diagnostics."""
import subprocess, serial, time, sys

print("Flashing firmware...")
result = subprocess.run(
    ["python", "-m", "esptool", "--chip", "esp32p4", "-p", "COM30", "-b", "460800",
     "write_flash", "0xB00000", r"firmware\neogeo_app.bin"],
    capture_output=True, text=True, cwd=r"C:\ESPIDFprojects\RetroESP32_P4"
)
if result.returncode != 0:
    print("Flash FAILED:", result.stderr[-200:])
    sys.exit(1)
print("Flash done, opening serial...")

time.sleep(0.5)
s = serial.Serial('COM30', 115200, timeout=1)
start = time.time()
lines = []
while time.time() - start < 30:
    line = s.readline()
    if line:
        decoded = line.decode('utf-8', 'replace').strip()
        lines.append(decoded)
s.close()

# Print KOF98-related lines
print("\n=== KOF98 DIAGNOSTIC LINES ===")
for l in lines:
    if any(k in l for k in ['KOF98', 'GEN68k', 'BIOS BYTE', 'Special init', 'DISPATCH', 'Selecting', 'BK_W', 'CPU_R']):
        print(l)

print(f"\n=== TOTAL: {len(lines)} lines captured ===")
# Also print lines 140-185 for context
print("\n=== INIT SECTION ===")
for i, l in enumerate(lines):
    if i >= 130 and i < 200:
        print(f"[{i:3d}] {l}")
