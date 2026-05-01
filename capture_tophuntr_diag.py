import serial, time

s = serial.Serial('COM30', 115200, timeout=2)
start = time.time()
lines = []
while time.time() - start < 90:
    l = s.readline()
    if l:
        txt = l.decode('utf-8', 'replace').strip()
        lines.append(txt)
        if 'CTRL=' in txt:
            # grab more CTRL lines
            count = 1
            while count < 25 and time.time() - start < 90:
                l2 = s.readline()
                if l2:
                    t2 = l2.decode('utf-8', 'replace').strip()
                    lines.append(t2)
                    if 'CTRL=' in t2:
                        count += 1
            break
s.close()

print(f"Total lines: {len(lines)}")
ctrl = [x for x in lines if 'CTRL=' in x]
print(f"CTRL lines: {len(ctrl)}")
for x in ctrl[:30]:
    print(x)
