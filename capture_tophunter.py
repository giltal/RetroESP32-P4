import serial, time

s = serial.Serial('COM30', 115200, timeout=2)
start = time.time()
lines = []
while time.time() - start < 45:
    l = s.readline()
    if l:
        lines.append(l.decode('utf-8', 'replace').strip())
s.close()

spr = [x for x in lines if 'SPR_Y' in x or 'NEO_CTL' in x]
print(f'Got {len(lines)} lines, {len(spr)} relevant lines:')
for x in spr[:25]:
    print(x)
