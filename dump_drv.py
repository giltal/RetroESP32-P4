import zipfile, struct, sys

REGIONS = {
    0: 'AUDIO_CPU_BIOS', 1: 'AUDIO_CPU_CART', 2: 'AUDIO_CPU_ENC',
    3: 'AUDIO_DATA_1', 4: 'AUDIO_DATA_2', 5: 'FIXED_BIOS',
    6: 'FIXED_CART', 7: 'MAIN_CPU_BIOS', 8: 'MAIN_CPU_CART',
    9: 'SPRITES', 10: 'SPR_USAGE', 11: 'GFIX_USAGE'
}

def read_drv(data):
    off = 0
    name = data[off:off+32].split(b'\x00')[0].decode(); off += 32
    parent = data[off:off+32].split(b'\x00')[0].decode(); off += 32
    longname = data[off:off+128].split(b'\x00')[0].decode(); off += 128
    year = struct.unpack_from('<I', data, off)[0]; off += 4
    romsizes = struct.unpack_from('<10I', data, off); off += 40
    nb_romfile = struct.unpack_from('<I', data, off)[0]; off += 4
    print(f'Name: {name}')
    print(f'Parent: {parent}')
    print(f'Long: {longname}')
    print(f'Year: {year}')
    print(f'Region sizes:')
    for i, s in enumerate(romsizes):
        if s > 0:
            rn = REGIONS.get(i, '?')
            print(f'  [{i}] {rn} = 0x{s:08x} ({s // 1024}KB)')
    print(f'ROM files: {nb_romfile}')
    for i in range(nb_romfile):
        fname = data[off:off+32].split(b'\x00')[0].decode(); off += 32
        region = data[off]; off += 1
        src, dest, size, crc = struct.unpack_from('<4I', data, off); off += 16
        rn = REGIONS.get(region, f'?{region}')
        print(f'  [{i:2d}] {fname:22s} region={region}({rn:16s}) src=0x{src:08x} dest=0x{dest:08x} size=0x{size:08x} crc=0x{crc:08x}')

games = sys.argv[1:] if len(sys.argv) > 1 else ['blazstar']
z = zipfile.ZipFile('SDcard/roms/neogeo/gngeo_data.zip')
for game in games:
    print(f'\n=== {game} ===')
    data = z.read(f'rom/{game}.drv')
    read_drv(data)
