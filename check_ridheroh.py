import zipfile, struct, os, sys

dzf = zipfile.ZipFile(r'C:\ESPIDFprojects\RetroESP32_P4\SDcard\roms\neogeo\gngeo_data.zip', 'r')
data = dzf.read('rom/ridheroh.drv')
off = 0
name = data[off:off+32].split(b'\x00')[0].decode()
off += 32
parent = data[off:off+32].split(b'\x00')[0].decode()
off += 32
off += 128  # longname
off += 4    # year
rs = [struct.unpack_from('<I', data, off + i*4)[0] for i in range(10)]
off += 40
nb = struct.unpack_from('<I', data, off)[0]
off += 4

print(f"Name: {name}, Parent: {parent}")
print(f"Sprite ROM size: {rs[9] // 1024 // 1024} MB")
print(f"Total ROMs: {nb}")
print("\nSprite ROMs (region=9):")

for _ in range(nb):
    fn = data[off:off+32].split(b'\x00')[0].decode()
    off += 32
    region = data[off]
    off += 1
    src = struct.unpack_from('<I', data, off)[0]; off += 4
    dest = struct.unpack_from('<I', data, off)[0]; off += 4
    size = struct.unpack_from('<I', data, off)[0]; off += 4
    crc = struct.unpack_from('<I', data, off)[0]; off += 4
    if region == 9:
        print(f"  {fn:20s} dest=0x{dest:08X} size=0x{size:06X} crc={crc:08X}")

# Also check parent
print("\n\n--- Parent: ridhero ---")
data2 = dzf.read('rom/ridhero.drv')
off = 0
name2 = data2[off:off+32].split(b'\x00')[0].decode(); off += 32
parent2 = data2[off:off+32].split(b'\x00')[0].decode(); off += 32
off += 128 + 4
rs2 = [struct.unpack_from('<I', data2, off + i*4)[0] for i in range(10)]; off += 40
nb2 = struct.unpack_from('<I', data2, off)[0]; off += 4

print(f"Name: {name2}, Parent: {parent2}")
print(f"Sprite ROM size: {rs2[9] // 1024 // 1024} MB")
print(f"Total ROMs: {nb2}")
print("\nSprite ROMs (region=9):")

for _ in range(nb2):
    fn = data2[off:off+32].split(b'\x00')[0].decode()
    off += 32
    region = data2[off]; off += 1
    src = struct.unpack_from('<I', data2, off)[0]; off += 4
    dest = struct.unpack_from('<I', data2, off)[0]; off += 4
    size = struct.unpack_from('<I', data2, off)[0]; off += 4
    crc = struct.unpack_from('<I', data2, off)[0]; off += 4
    if region == 9:
        print(f"  {fn:20s} dest=0x{dest:08X} size=0x{size:06X} crc={crc:08X}")

dzf.close()
