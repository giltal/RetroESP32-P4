import serial, time

s = serial.Serial('COM30', 115200, timeout=2)
start = time.time()
lines = []
while time.time() - start < 15:
    l = s.readline()
    if l:
        lines.append(l.decode('utf-8', 'replace').strip())
s.close()

keywords = ['FPS', 'stream', 'tiles', 'decrypt', 'ROM sizes', 'budget', 'cache', 'Streaming', 'LOAD', 'sprite', 'SKIP', 'Loading', 'zupapa', 'game', 'name', 'ROM path']
for line in lines:
    if line and any(k.lower() in line.lower() for k in keywords):
        print(line)
