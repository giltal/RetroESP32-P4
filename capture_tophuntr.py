import serial, time

s = serial.Serial('COM30', 115200, timeout=2)
time.sleep(0.5)
s.reset_input_buffer()

# Wait for game to reach frame 300 dump
print("Waiting for F900 sprite dump...")
start = time.time()
lines = []
while time.time() - start < 40:
    l = s.readline()
    if l:
        line = l.decode('utf-8', 'replace').strip()
        lines.append(line)
        if 'F900' in line:
            print(line)

s.close()
print(f"\nGot {len(lines)} lines total")
# Print RENDER lines
for l in lines:
    if 'RENDER' in l:
        print(l)
        break
