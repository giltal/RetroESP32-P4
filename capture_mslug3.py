import serial, time

s = serial.Serial('COM30', 115200, timeout=2)
start = time.time()
lines = []
while time.time() - start < 90:
    l = s.readline()
    if l:
        line = l.decode('utf-8', 'replace').strip()
        lines.append(line)
        if 'abort' in line.lower() or 'Error alloc' in line or len(lines) > 600:
            break
s.close()

print(f'Got {len(lines)} lines')
keywords = ['ROM size', 'free', 'Streaming', 'Error', 'alloc', 'abort',
            'ADPCM', 'adpcm', 'budget', 'OOM', 'heap', 'not enough',
            'tiles=', 'PSRAM', 'cache', 'streaming', 'region', 'FPS',
            'Loading', 'memory', 'setup', 'vroma', 'Sprite']
for x in lines:
    if any(k in x for k in keywords):
        print(x)
