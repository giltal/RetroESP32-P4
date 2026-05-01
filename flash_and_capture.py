import serial, time, subprocess, sys, threading

COM_PORT = 'COM30'
BAUD = 115200
FLASH_CMD = [
    sys.executable, '-m', 'esptool',
    '--chip', 'esp32p4', '-p', COM_PORT, '-b', '460800',
    'write_flash', '0xB00000', 'firmware\\neogeo_app.bin'
]

# Flash first (causes reset)
print("Flashing firmware...")
result = subprocess.run(FLASH_CMD, capture_output=True, text=True)
if result.returncode != 0:
    print("Flash failed:")
    print(result.stderr)
    sys.exit(1)
print("Flash done, opening serial...")

# Open serial immediately after flash (device resets after flash)
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
        # Stop early if we see game running
        if 'Selecting' in line or 'piracy' in line.lower():
            # Grab a few more lines
            for _ in range(20):
                l2 = s.readline()
                if l2:
                    lines.append(l2.decode('utf-8', 'replace').strip())
            break

s.close()

print(f"\n=== CAPTURED {len(lines)} LINES ===")
for i, l in enumerate(lines):
    ll = l.lower()
    if any(k in ll for k in ['kof98', 'post-init', 'swap', 'already le', 'is be',
                               'game rom', 'rom[0', 'fetch16', 'gv[0', 'vector',
                               'cpu_68k', 'selecting', 'prot', 'dispatch',
                               'bios', 'init_kof', 'decrypt']):
        print(f'[{i}] {l}')

print("\n=== LAST 30 LINES ===")
for l in lines[-30:]:
    print(l)
