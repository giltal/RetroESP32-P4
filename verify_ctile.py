#!/usr/bin/env python3
"""Verify .ctile file against game ZIP by re-converting tiles and comparing."""
import struct, zipfile, sys, os

CROM_PAIRS = [
    ("239-c1.c1", "239-c2.c2", 0x0000000, 0x400000),
    ("239-c3.c3", "239-c4.c4", 0x0800000, 0x400000),
    ("239-c5.c5", "239-c6.c6", 0x1000000, 0x400000),
    ("239-c7.c7", "239-c8.c8", 0x1800000, 0x400000),
]
TOTAL_SIZE = 0x2000000

def convert_tile(tile_data):
    swap = bytearray(tile_data)
    out = bytearray(128)
    idx = 0
    for y in range(16):
        dw = 0
        for x in range(8):
            pen  = ((swap[64 + (y<<2) + 3] >> x) & 1) << 3
            pen |= ((swap[64 + (y<<2) + 1] >> x) & 1) << 2
            pen |= ((swap[64 + (y<<2) + 2] >> x) & 1) << 1
            pen |=  (swap[64 + (y<<2)    ] >> x) & 1
            dw |= pen << ((7-x) << 2)
        struct.pack_into('<I', out, idx, dw); idx += 4
        dw = 0
        for x in range(8):
            pen  = ((swap[(y<<2) + 3] >> x) & 1) << 3
            pen |= ((swap[(y<<2) + 1] >> x) & 1) << 2
            pen |= ((swap[(y<<2) + 2] >> x) & 1) << 1
            pen |=  (swap[(y<<2)    ] >> x) & 1
            dw |= pen << ((7-x) << 2)
        struct.pack_into('<I', out, idx, dw); idx += 4
    return bytes(out)

zip_path = "SDcard/roms/neogeo/blazstar.zip"
ctile_path = "SDcard/roms/neogeo/blazstar.ctile"

print("Loading C ROMs and interleaving...")
buf = bytearray(TOTAL_SIZE)
with zipfile.ZipFile(zip_path, 'r') as zf:
    for even_name, odd_name, dest_base, rom_size in CROM_PAIRS:
        even = zf.read(even_name)
        odd = zf.read(odd_name)
        for i in range(rom_size):
            buf[dest_base + i*2] = even[i]
            buf[dest_base + i*2 + 1] = odd[i]

print("Verifying tiles against .ctile file...")
nb_tiles = TOTAL_SIZE >> 7
mismatches = 0
first_mismatch = None

with open(ctile_path, 'rb') as f:
    for tile in range(nb_tiles):
        raw = buf[tile*128 : tile*128+128]
        expected = convert_tile(raw)
        actual = f.read(128)
        if expected != actual:
            mismatches += 1
            if first_mismatch is None:
                first_mismatch = tile
                print(f"\nFIRST MISMATCH at tile {tile} (offset 0x{tile*128:08X}):")
                print(f"  Expected: {expected[:32].hex()}")
                print(f"  Actual:   {actual[:32].hex()}")
                print(f"  Raw src:  {raw[:32].hex()}")
            if mismatches <= 5:
                print(f"  Mismatch tile {tile}")

print(f"\nResult: {mismatches}/{nb_tiles} tiles differ")
if mismatches == 0:
    print("PASS: .ctile file matches expected conversion perfectly")
else:
    print(f"FAIL: {mismatches} mismatches (first at tile {first_mismatch})")
