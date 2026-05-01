import serial, time

s = serial.Serial('COM30', 115200, timeout=2)
print("Waiting for boot output... power cycle NOW")
start = time.time()
lines = []
while time.time() - start < 60:
    l = s.readline()
    if l:
        lines.append(l.decode('utf-8', 'replace').strip())
        if len(lines) > 400:
            break
s.close()
print(f"Got {len(lines)} lines:")
for line in lines:
    print(line)
