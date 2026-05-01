import serial, time

s = serial.Serial('COM30', 115200, timeout=2)
start = time.time()
lines = []

while time.time() - start < 45:
    l = s.readline()
    if l:
        lines.append(l.decode('utf-8', 'replace').strip())

s.close()
print(f'Got {len(lines)} lines')
for line in lines[-100:]:
    print(line)
