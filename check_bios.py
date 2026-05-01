import serial, time

s = serial.Serial('COM30', 115200, timeout=2)
start = time.time()
lines = []
while time.time() - start < 20:
    l = s.readline()
    if l:
        lines.append(l.decode('utf-8', 'replace').strip())
s.close()

print(f"Total lines: {len(lines)}")
errs = [x for x in lines if 'ERROR' in x or 'VIDEO RAM' in x]
if errs:
    print("ERRORS FOUND:")
    for e in errs:
        print(f"  {e}")
else:
    print("No BIOS errors detected")
