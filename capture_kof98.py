import serial, time, sys

s = serial.Serial('COM30', 115200, timeout=1)
start = time.time()
lines = []
while time.time() - start < 50:
    l = s.readline()
    if l:
        lines.append(l.decode('utf-8', 'replace').strip())
s.close()

# Save full log
with open('kof98_capture.txt', 'w', encoding='utf-8') as f:
    for x in lines:
        f.write(x + '\n')

# Print relevant lines
for x in lines:
    if x and ('KOF98' in x or 'BK_WR' in x or 'Selecting' in x or 'FPS:' in x or 'GEN68k' in x or 'kof98' in x or 'prot' in x):
        print(x)
