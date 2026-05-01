import serial, time
s = serial.Serial('COM30', 115200, timeout=2)
s.dtr = False
s.rts = True
time.sleep(0.1)
s.rts = False
time.sleep(0.1)
s.reset_input_buffer()
lines = []
for _ in range(800):
    l = s.readline()
    if l:
        lines.append(l.decode('utf-8', 'replace').strip())
print(f'TOTAL LINES: {len(lines)}')
for i, l in enumerate(lines):
    ll = l.lower()
    if any(k in ll for k in ['kof98', 'post-init', 'swap', 'already le', 'is be',
                               'game rom', 'rom[0', 'fetch', 'gv[0', 'vector',
                               'cpu_68k', 'init_neo', 'selecting', 'prot']):
        print(f'[{i}] {l}')
# Print 20 lines around 'Selecting'
for i, l in enumerate(lines):
    if 'Selecting' in l:
        for j in range(max(0, i - 2), min(len(lines), i + 25)):
            print(f'  [{j}] {lines[j]}')
        break
s.close()
