import serial, time

s = serial.Serial('COM30', 115200, timeout=2)
start = time.time()
lines = []

print("Monitoring for crash... (120s timeout)")
while time.time() - start < 120:
    l = s.readline()
    if l:
        line = l.decode('utf-8', 'replace').strip()
        lines.append(line)
        # Print crash-related lines immediately
        if any(k in line.lower() for k in ['panic', 'guru', 'abort', 'assert', 'backtrace', 'fault', 'exception', 'invalid instruction', 'stack', 'epc', 'reset', 'reboot', 'watchdog', 'wdt', 'crash']):
            print(f"*** [{time.time()-start:.1f}s] {line}")

s.close()
print(f"\nGot {len(lines)} lines total")
print("\n=== LAST 150 LINES ===")
for line in lines[-150:]:
    print(line)
