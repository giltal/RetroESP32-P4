import serial, time, subprocess, sys

COM_PORT = 'COM30'
BAUD = 115200
FLASH_CMD = [
    sys.executable, '-m', 'esptool',
    '--chip', 'esp32p4', '-p', COM_PORT, '-b', '460800',
    'write_flash', '0xB00000', 'firmware\\neogeo_app.bin'
]

print("Flashing firmware...")
result = subprocess.run(FLASH_CMD, capture_output=True, text=True)
if result.returncode != 0:
    print("Flash failed:", result.stderr)
    sys.exit(1)
print("Flash done, opening serial...")

time.sleep(0.5)
s = serial.Serial(COM_PORT, BAUD, timeout=2)
s.reset_input_buffer()

lines = []
start = time.time()
while time.time() - start < 90:
    l = s.readline()
    if l:
        line = l.decode('utf-8', 'replace').strip()
        lines.append(line)
        if 'Selecting' in line:
            for _ in range(20):
                l2 = s.readline()
                if l2:
                    lines.append(l2.decode('utf-8', 'replace').strip())
            break

s.close()

print(f"\n=== ALL {len(lines)} LINES ===")
for i, l in enumerate(lines):
    print(f'[{i:3d}] {l}')
