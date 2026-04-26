#!/usr/bin/env python3
"""
Generate .vroma cache file for Neo Geo ADPCM-A audio ROMs.
Reads V ROM files from a game ZIP, concatenates them at their
destination offsets, and outputs a flat binary ready for the ESP32 emulator.

Usage: python gen_vrom.py <game.zip> [output_dir]
  output_dir defaults to same directory as the ZIP file.

The script auto-detects the game's ROM driver from gngeo_data.zip
(expected alongside the game ZIP or in SDcard/roms/neogeo/).
"""

import sys
import os
import struct
import zipfile
import time

# Region IDs from the driver format (must match gngeo)
REGION_AUDIO_DATA_1 = 3
REGION_AUDIO_DATA_2 = 4

def read_drv(data):
    """Parse a .drv ROM definition file."""
    off = 0
    name = data[off:off+32].split(b'\x00')[0].decode(); off += 32
    parent = data[off:off+32].split(b'\x00')[0].decode(); off += 32
    longname = data[off:off+128].split(b'\x00')[0].decode(); off += 128
    year = struct.unpack_from('<I', data, off)[0]; off += 4
    romsizes = list(struct.unpack_from('<10I', data, off)); off += 40
    nb_romfile = struct.unpack_from('<I', data, off)[0]; off += 4

    roms = []
    for i in range(nb_romfile):
        fname = data[off:off+32].split(b'\x00')[0].decode(); off += 32
        region = data[off]; off += 1
        src, dest, size, crc = struct.unpack_from('<4I', data, off); off += 16
        roms.append({
            'filename': fname, 'region': region,
            'src': src, 'dest': dest, 'size': size, 'crc': crc
        })

    return {
        'name': name, 'parent': parent, 'longname': longname,
        'year': year, 'romsizes': romsizes, 'roms': roms
    }


def find_gngeo_data(game_zip_path):
    """Locate gngeo_data.zip — try same dir as game ZIP, then SDcard/roms/neogeo/."""
    candidates = [
        os.path.join(os.path.dirname(game_zip_path), 'gngeo_data.zip'),
        os.path.join(os.path.dirname(os.path.abspath(__file__)),
                     'SDcard', 'roms', 'neogeo', 'gngeo_data.zip'),
    ]
    for c in candidates:
        if os.path.isfile(c):
            return c
    return None


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <game.zip> [output_dir]")
        sys.exit(1)

    game_zip_path = sys.argv[1]
    output_dir = sys.argv[2] if len(sys.argv) > 2 else os.path.dirname(game_zip_path) or '.'

    game_name = os.path.splitext(os.path.basename(game_zip_path))[0]

    # Load driver
    gngeo_path = find_gngeo_data(game_zip_path)
    if not gngeo_path:
        print("ERROR: Cannot find gngeo_data.zip")
        sys.exit(1)

    with zipfile.ZipFile(gngeo_path) as gz:
        drv_data = gz.read(f'rom/{game_name}.drv')
    drv = read_drv(drv_data)

    adpcma_size = drv['romsizes'][REGION_AUDIO_DATA_1]
    adpcmb_size = drv['romsizes'][REGION_AUDIO_DATA_2]

    if adpcma_size == 0 and adpcmb_size == 0:
        print(f"No ADPCM data for {game_name}")
        sys.exit(0)

    print(f"Game: {drv['longname']} ({game_name})")
    print(f"ADPCM-A size: {adpcma_size // 1024} KB")
    if adpcmb_size > 0:
        print(f"ADPCM-B size: {adpcmb_size // 1024} KB")

    # Collect V ROM files from driver
    vroma_files = []
    vromb_files = []
    for rom in drv['roms']:
        if rom['region'] == REGION_AUDIO_DATA_1:
            vroma_files.append(rom)
        elif rom['region'] == REGION_AUDIO_DATA_2:
            vromb_files.append(rom)

    game_zip = zipfile.ZipFile(game_zip_path)

    # Also check parent ZIP for shared files
    parent_zip = None
    if drv['parent']:
        parent_path = os.path.join(os.path.dirname(game_zip_path),
                                   drv['parent'] + '.zip')
        if os.path.isfile(parent_path):
            parent_zip = zipfile.ZipFile(parent_path)

    def read_rom_file(filename, crc):
        """Try game ZIP first, then parent ZIP. Match by CRC if name fails."""
        for z in [game_zip, parent_zip]:
            if z is None:
                continue
            namelist = z.namelist()
            # Try exact name match (case-insensitive)
            for zn in namelist:
                if zn.lower() == filename.lower():
                    return z.read(zn)
            # Fall back to CRC match
            if crc != 0:
                for info in z.infolist():
                    if info.CRC == crc:
                        print(f"    CRC match: {filename} -> {info.filename}")
                        return z.read(info.filename)
        raise FileNotFoundError(f"ROM file '{filename}' (crc=0x{crc:08x}) not found in ZIP(s)")

    # Generate ADPCM-A cache
    if adpcma_size > 0 and vroma_files:
        out_path = os.path.join(output_dir, f'{game_name}.vroma')
        print(f"\nGenerating {out_path} ({adpcma_size // (1024*1024)} MB)...")
        t0 = time.time()

        buf = bytearray(adpcma_size)
        for rom in vroma_files:
            print(f"  Reading {rom['filename']} -> offset 0x{rom['dest']:08X}, {rom['size'] // 1024} KB")
            data = read_rom_file(rom['filename'], rom['crc'])
            if len(data) < rom['size']:
                print(f"    WARNING: file is {len(data)} bytes, expected {rom['size']}")
            dest = rom['dest']
            copy_len = min(len(data), rom['size'], adpcma_size - dest)
            buf[dest:dest + copy_len] = data[:copy_len]

        with open(out_path, 'wb') as f:
            f.write(buf)

        elapsed = time.time() - t0
        print(f"  Done in {elapsed:.1f}s — {os.path.getsize(out_path)} bytes")

    # Generate ADPCM-B cache (if different from A)
    if adpcmb_size > 0 and vromb_files:
        out_path = os.path.join(output_dir, f'{game_name}.vromb')
        print(f"\nGenerating {out_path} ({adpcmb_size // (1024*1024)} MB)...")
        t0 = time.time()

        buf = bytearray(adpcmb_size)
        for rom in vromb_files:
            print(f"  Reading {rom['filename']} -> offset 0x{rom['dest']:08X}, {rom['size'] // 1024} KB")
            data = read_rom_file(rom['filename'], rom['crc'])
            dest = rom['dest']
            copy_len = min(len(data), rom['size'], adpcmb_size - dest)
            buf[dest:dest + copy_len] = data[:copy_len]

        with open(out_path, 'wb') as f:
            f.write(buf)

        elapsed = time.time() - t0
        print(f"  Done in {elapsed:.1f}s — {os.path.getsize(out_path)} bytes")

    if parent_zip:
        parent_zip.close()
    game_zip.close()

    print("\nAll ADPCM cache files generated successfully.")
    print("Upload with:")
    if adpcma_size > 0:
        print(f"  python tools/upload_papp.py {game_name}.vroma --dest /sd/roms/neogeo/{game_name}.vroma")
    if adpcmb_size > 0 and vromb_files:
        print(f"  python tools/upload_papp.py {game_name}.vromb --dest /sd/roms/neogeo/{game_name}.vromb")


if __name__ == '__main__':
    main()
