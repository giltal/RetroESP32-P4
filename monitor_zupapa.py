import serial, time

s = serial.Serial('COM30', 115200, timeout=2)
print('Monitoring zupapa... launch the game NOW')
start = time.time()
lines = []
while time.time() - start < 90:
    l = s.readline()
    if l:
        line = l.decode('utf-8', 'replace').strip()
        lines.append(line)
        print(line)
        if len(lines) > 800:
            break
s.close()
print(f'\n=== Got {len(lines)} lines in {time.time()-start:.1f}s ===')
