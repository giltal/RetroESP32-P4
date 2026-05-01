#!/usr/bin/env python3
"""
Generate Neo Geo cache files (.ctile, .cusage, .vroma) for any game.
Reads the ROM driver from gngeo_data.zip to auto-detect C ROM pairs,
V ROM files, and sizes. No hardcoded game-specific data.

Usage: python gen_cache.py <game_name> [--rom-dir DIR] [--out-dir DIR]

  game_name   : short name, e.g. "blazstar", "mslug", "kof99"
  --rom-dir   : directory containing <game>.zip and gngeo_data.zip
                (default: same directory as this script)
  --out-dir   : output directory for cache files
                (default: same as --rom-dir)

Examples:
  python gen_cache.py blazstar
  python gen_cache.py mslug --rom-dir C:\\roms --out-dir C:\\output
"""

import sys
import os
import struct
import zipfile
import time
import argparse

# Region constants (must match gngeo roms.h)
REGION_AUDIO_CPU_CART = 1
REGION_AUDIO_DATA_1   = 3   # ADPCM-A
REGION_AUDIO_DATA_2   = 4   # ADPCM-B
REGION_FIXED_LAYER_CARTRIDGE = 6  # S ROM (fix layer)
REGION_SPRITES        = 9   # C ROMs

# Sprite usage magic (must match roms.c and gen_ctile.py)
CUSAGE_MAGIC   = 0x43553039  # "CU09"
TILE_INVISIBLE = 1

# Games that use CMC42 GFX encryption: {game_name: extra_xor}
CMC42_GAMES = {
    "kof99":    0x00,
    "kof99n":   0x00,
    "garou":    0x06,
    "garouo":   0x06,
    "garoubl":  0x06,
    "mslug3":   0xad,
    "mslug3h":  0xad,
    "mslug3n":  0xad,
    "ganryu":   0x07,
    "s1945p":   0x05,
    "preisle2": 0x9f,
    "bangbead": 0xf8,
    "nitd":     0xff,
    "zupapa":   0xbd,
    "sengoku3": 0xfe,
}

# Games that use CMC50 GFX encryption (different XOR tables)
CMC50_GAMES = {
    "kof2000":  0xad,
    "kof2000n": 0xad,
    "kof2001":  0x1e,
    "mslug4":   0x31,
    "ms4plus":  0x31,
    "rotd":     0x3f,
    "kof2002":  0xec,
    "kof2002b": 0xec,
    "kf2k2pls": 0xec,
    "kf2k2mp":  0xec,
    "kof2km2":  0xec,
    "matrim":   0x6a,
    "pnyaa":    0x2e,
    "mslug5":   0x19,
    "ms5pcb":   0x19,
    "svc":      0x57,
    "svcpcb":   0x57,
    "kof2003":  0x9d,
    "kof2003n": 0x9d,
    "kf2k3pcb": 0x9d,
    "samsh5sp": 0x0d,
    "samsho5":  0x0f,
}


def read_drv(data):
    """Parse a .drv ROM definition file from gngeo_data.zip."""
    off = 0
    name = data[off:off+32].split(b'\x00')[0].decode(); off += 32
    parent = data[off:off+32].split(b'\x00')[0].decode(); off += 32
    longname = data[off:off+128].split(b'\x00')[0].decode(); off += 128
    year = struct.unpack_from('<I', data, off)[0]; off += 4
    romsizes = list(struct.unpack_from('<10I', data, off)); off += 40
    nb_romfile = struct.unpack_from('<I', data, off)[0]; off += 4

    roms = []
    for _ in range(nb_romfile):
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


def convert_roms_tile(tile_data):
    """Convert one 128-byte raw tile to decoded 4bpp format.
    Exactly matches convert_roms_tile() in roms.c."""
    swap = bytearray(tile_data)
    out = bytearray(128)
    usage = 0
    out_idx = 0

    for y in range(16):
        # First dword: bits from swap[64..] region
        dw = 0
        for x in range(8):
            pen  = ((swap[64 + (y << 2) + 3] >> x) & 1) << 3
            pen |= ((swap[64 + (y << 2) + 1] >> x) & 1) << 2
            pen |= ((swap[64 + (y << 2) + 2] >> x) & 1) << 1
            pen |=  (swap[64 + (y << 2)]     >> x) & 1
            dw |= pen << ((7 - x) << 2)
            usage |= (1 << pen)
        struct.pack_into('<I', out, out_idx, dw)
        out_idx += 4

        # Second dword: bits from swap[0..] region
        dw = 0
        for x in range(8):
            pen  = ((swap[(y << 2) + 3] >> x) & 1) << 3
            pen |= ((swap[(y << 2) + 1] >> x) & 1) << 2
            pen |= ((swap[(y << 2) + 2] >> x) & 1) << 1
            pen |=  (swap[(y << 2)]     >> x) & 1
            dw |= pen << ((7 - x) << 2)
            usage |= (1 << pen)
        struct.pack_into('<I', out, out_idx, dw)
        out_idx += 4

    return bytes(out), usage


# ---------------------------------------------------------------------------
# CMC42/CMC50 GFX Decryption (Python port of neocrypt.c)
# ---------------------------------------------------------------------------

def load_xor_tables(datafile_path, table_name):
    """Load XOR tables from gngeo_data.zip."""
    with zipfile.ZipFile(datafile_path, 'r') as zf:
        data = zf.read(f'rom/{table_name}')

    return (
        data[0:256],      # type0_t03
        data[256:512],    # type0_t12
        data[512:768],    # type1_t03
        data[768:1024],   # type1_t12
        data[1024:1280],  # address_8_15_xor1
        data[1280:1536],  # address_8_15_xor2
        data[1536:1792],  # address_16_23_xor1
        data[1792:2048],  # address_16_23_xor2
        data[2048:2304],  # address_0_7_xor
    )


def _decrypt_byte_pair(c0, c1, table0hi, table0lo, table1, base, invert, addr_0_7_xor):
    """Decrypt one byte pair. Matches decrypt() in neocrypt.c."""
    tmp = table1[(base & 0xff) ^ addr_0_7_xor[(base >> 8) & 0xff]]
    xor0 = (table0hi[(base >> 8) & 0xff] & 0xfe) | (tmp & 0x01)
    xor1 = (tmp & 0xfe) | (table0lo[(base >> 8) & 0xff] & 0x01)

    if invert:
        return (c1 ^ xor0) & 0xff, (c0 ^ xor1) & 0xff
    else:
        return (c0 ^ xor0) & 0xff, (c1 ^ xor1) & 0xff


def neogeo_gfx_decrypt(rom, rom_size, extra_xor, tables):
    """Apply CMC42/CMC50 GFX decryption in-place."""
    (type0_t03, type0_t12, type1_t03, type1_t12,
     addr_8_15_x1, addr_8_15_x2, addr_16_23_x1, addr_16_23_x2, addr_0_7_x) = tables

    quarter = rom_size // 4

    # Phase 1: Data XOR -> buf
    print("  Phase 1: Data XOR decryption...")
    buf = bytearray(rom_size)
    t0 = time.time()

    for rpos in range(quarter):
        off = 4 * rpos
        r0, r3 = _decrypt_byte_pair(
            rom[off+0], rom[off+3],
            type0_t03, type0_t12, type1_t03,
            rpos, (rpos >> 8) & 1, addr_0_7_x)
        invert2 = ((rpos >> 16) ^ addr_16_23_x2[(rpos >> 8) & 0xff]) & 1
        r1, r2 = _decrypt_byte_pair(
            rom[off+1], rom[off+2],
            type0_t12, type0_t03, type1_t12,
            rpos, invert2, addr_0_7_x)

        buf[off+0] = r0
        buf[off+1] = r1
        buf[off+2] = r2
        buf[off+3] = r3

        if rpos > 0 and rpos % 2000000 == 0:
            elapsed = time.time() - t0
            pct = 100 * rpos // quarter
            print(f"    {pct}% ({elapsed:.1f}s)")

    # Phase 2: Address XOR (shuffle)
    print("  Phase 2: Address XOR shuffle...")
    t1 = time.time()

    for rpos in range(quarter):
        baser = rpos
        baser ^= extra_xor
        baser ^= addr_8_15_x1[(baser >> 16) & 0xff] << 8
        baser ^= addr_8_15_x2[baser & 0xff] << 8
        baser ^= addr_16_23_x1[baser & 0xff] << 16
        baser ^= addr_16_23_x2[(baser >> 8) & 0xff] << 16
        baser ^= addr_0_7_x[(baser >> 8) & 0xff]

        if rom_size == 0x3000000:  # preisle2
            if rpos < 0x2000000 // 4:
                baser &= (0x2000000 // 4) - 1
            else:
                baser = 0x2000000 // 4 + (baser & ((0x1000000 // 4) - 1))
        elif rom_size == 0x6000000:  # kf2k3pcb
            if rpos < 0x4000000 // 4:
                baser &= (0x4000000 // 4) - 1
            else:
                baser = 0x4000000 // 4 + (baser & ((0x1000000 // 4) - 1))
        else:
            baser &= (rom_size // 4) - 1

        off_dst = 4 * rpos
        off_src = 4 * baser
        rom[off_dst+0] = buf[off_src+0]
        rom[off_dst+1] = buf[off_src+1]
        rom[off_dst+2] = buf[off_src+2]
        rom[off_dst+3] = buf[off_src+3]

        if rpos > 0 and rpos % 2000000 == 0:
            elapsed = time.time() - t1
            pct = 100 * rpos // quarter
            print(f"    {pct}% ({elapsed:.1f}s)")

    print(f"  Decrypt complete: data={t1-t0:.1f}s addr={time.time()-t1:.1f}s")


def neogeo_sfix_extract(decrypted_rom, rom_size, tx_size):
    """Extract and shuffle SFIX data from the end of decrypted C ROM.
    Matches neogeo_sfix_decrypt() in neocrypt.c."""
    src = decrypted_rom[rom_size - tx_size:]
    dst = bytearray(tx_size)
    for i in range(tx_size):
        src_idx = (i & ~0x1f) + ((i & 7) << 2) + ((~i & 8) >> 2) + ((i & 0x10) >> 4)
        dst[i] = src[src_idx]
    return bytes(dst)


def make_rom_reader(game_zip, parent_zip):
    """Return a function that reads ROM files from game ZIP or parent ZIP."""
    def read_rom_file(filename, crc=0):
        stem = os.path.splitext(filename)[0].lower()
        for z in [game_zip, parent_zip]:
            if z is None:
                continue
            # Try exact name match (case-insensitive)
            for zn in z.namelist():
                if zn.lower() == filename.lower():
                    return z.read(zn)
            # Try stem match (e.g. "239-c1.bin" matches "239-c1.c1")
            for zn in z.namelist():
                if os.path.splitext(zn)[0].lower() == stem:
                    print(f"    stem match: {filename} -> {zn}")
                    return z.read(zn)
            # Fall back to CRC match
            if crc != 0:
                for info in z.infolist():
                    if info.CRC == (crc & 0xFFFFFFFF):
                        print(f"    CRC match: {filename} -> {info.filename}")
                        return z.read(info.filename)
        raise FileNotFoundError(
            f"ROM file '{filename}' (crc=0x{crc:08x}) not found in ZIP(s)")
    return read_rom_file


def get_crom_pairs(drv):
    """Extract C ROM interleave pairs from the driver.
    Returns list of (even_file, odd_file, dest_base, size) tuples."""
    sprite_roms = [r for r in drv['roms'] if r['region'] == REGION_SPRITES]
    # Group by dest with bit 0 stripped (the interleave base)
    bases = {}
    for r in sprite_roms:
        base = r['dest'] & ~1
        is_odd = r['dest'] & 1
        if base not in bases:
            bases[base] = [None, None]
        bases[base][is_odd] = r

    pairs = []
    for base in sorted(bases.keys()):
        even, odd = bases[base]
        if even is None or odd is None:
            print(f"  WARNING: incomplete pair at base 0x{base:08X}")
            continue
        pairs.append((even['filename'], odd['filename'],
                       even['crc'], odd['crc'], base, even['size']))

    return pairs


def generate_ctile(drv, read_rom, output_dir, game_name, gngeo_data_path):
    """Generate .ctile and .cusage files."""
    sprite_size = drv['romsizes'][REGION_SPRITES]
    if sprite_size == 0:
        print("No sprite data for this game.")
        return

    pairs = get_crom_pairs(drv)
    if not pairs:
        print("ERROR: No C ROM pairs found in driver.")
        return

    # Detect encryption
    encrypt_type = None
    extra_xor = None
    if game_name in CMC42_GAMES:
        encrypt_type = 'cmc42'
        extra_xor = CMC42_GAMES[game_name]
    elif game_name in CMC50_GAMES:
        encrypt_type = 'cmc50'
        extra_xor = CMC50_GAMES[game_name]

    game_dir = os.path.join(output_dir, game_name)
    os.makedirs(game_dir, exist_ok=True)
    ctile_path = os.path.join(game_dir, f"{game_name}.ctile")
    cusage_path = os.path.join(game_dir, f"{game_name}.cusage")

    print(f"\n=== Sprite Cache (.ctile + .cusage) ===")
    print(f"Total sprite size: {sprite_size // (1024*1024)} MB")
    print(f"C ROM pairs: {len(pairs)}")
    if encrypt_type:
        print(f"Encryption: {encrypt_type.upper()} (extra_xor=0x{extra_xor:02x})")
    else:
        print(f"Encryption: none")

    # Allocate interleaved buffer
    tiles_buf = bytearray(sprite_size)

    t0 = time.time()

    # Read and interleave C ROM pairs
    for even_name, odd_name, even_crc, odd_crc, dest_base, rom_size in pairs:
        print(f"  Interleaving {even_name} + {odd_name} "
              f"-> offset 0x{dest_base:07X} ({rom_size // (1024*1024)} MB each)")

        even_data = read_rom(even_name, even_crc)
        odd_data = read_rom(odd_name, odd_crc)

        if len(even_data) < rom_size:
            print(f"    WARNING: {even_name} is {len(even_data)} bytes, "
                  f"expected {rom_size}")
        if len(odd_data) < rom_size:
            print(f"    WARNING: {odd_name} is {len(odd_data)} bytes, "
                  f"expected {rom_size}")

        # Interleave: even bytes at dest+0,+2,+4,...  odd at dest+1,+3,+5,...
        actual_size = min(rom_size, len(even_data), len(odd_data))
        for i in range(actual_size):
            tiles_buf[dest_base + i * 2] = even_data[i]
            tiles_buf[dest_base + i * 2 + 1] = odd_data[i]

    t1 = time.time()
    print(f"Interleave done in {t1 - t0:.1f}s")

    # Apply GFX decryption if needed
    if encrypt_type is not None:
        table_file = f"{encrypt_type}.xor"
        print(f"Loading {table_file} from gngeo_data.zip...")
        tables = load_xor_tables(gngeo_data_path, table_file)
        print(f"Decrypting ({encrypt_type}, extra_xor=0x{extra_xor:02x})...")
        neogeo_gfx_decrypt(tiles_buf, sprite_size, extra_xor, tables)
        t1b = time.time()
        print(f"Decryption done in {t1b - t1:.1f}s")
        t1 = t1b

        # Extract SFIX (fix layer) from end of decrypted C ROM
        # neogeo_sfix_decrypt() in neocrypt.c does this but is skipped in streaming mode
        tx_size = drv['romsizes'][REGION_FIXED_LAYER_CARTRIDGE]
        if tx_size > 0 and tx_size <= sprite_size:
            print(f"Extracting SFIX ({tx_size // 1024} KB) from end of decrypted C ROM...")
            sfix_data = neogeo_sfix_extract(tiles_buf, sprite_size, tx_size)
            sfix_path = os.path.join(game_dir, f"{game_name}.sfix")
            with open(sfix_path, 'wb') as f:
                f.write(sfix_data)
            print(f"  {sfix_path}: {len(sfix_data):,} bytes")
        else:
            print(f"  SFIX: no fixed region in driver (size={tx_size})")

    # Convert tiles and build spr_usage
    nb_tiles = sprite_size >> 7   # 128 bytes per tile
    nb_usage = nb_tiles >> 4      # one uint32 per 16 tiles
    spr_usage = [0] * nb_usage

    print(f"Converting {nb_tiles:,} tiles...")
    converted_buf = bytearray(sprite_size)
    tiles_visible = 0

    for tile in range(nb_tiles):
        offset = tile << 7
        raw = tiles_buf[offset:offset + 128]
        conv, usage = convert_roms_tile(raw)
        converted_buf[offset:offset + 128] = conv

        if (usage & ~1) == 0:
            spr_usage[tile >> 4] |= (TILE_INVISIBLE << ((tile & 0xF) * 2))
        else:
            tiles_visible += 1

        if tile > 0 and tile % 100000 == 0:
            print(f"  {tile:,}/{nb_tiles:,} ({100 * tile // nb_tiles}%)")

    t2 = time.time()
    print(f"Conversion done in {t2 - t1:.1f}s  "
          f"({tiles_visible:,} visible, {nb_tiles - tiles_visible:,} invisible)")

    # Write .ctile
    print(f"Writing {ctile_path} ({sprite_size // (1024*1024)} MB)...")
    with open(ctile_path, 'wb') as f:
        f.write(converted_buf)

    # Write .cusage
    usage_data = struct.pack('<I', CUSAGE_MAGIC)
    for u in spr_usage:
        usage_data += struct.pack('<I', u)

    print(f"Writing {cusage_path} ({len(usage_data):,} bytes)...")
    with open(cusage_path, 'wb') as f:
        f.write(usage_data)

    t3 = time.time()
    print(f"Sprite cache done! {t3 - t0:.1f}s total")
    print(f"  {ctile_path}: {os.path.getsize(ctile_path):,} bytes")
    print(f"  {cusage_path}: {os.path.getsize(cusage_path):,} bytes")


def generate_vroma(drv, read_rom, output_dir, game_name):
    """Generate .vroma (and .vromb if needed) ADPCM cache files."""
    adpcma_size = drv['romsizes'][REGION_AUDIO_DATA_1]
    adpcmb_size = drv['romsizes'][REGION_AUDIO_DATA_2]

    if adpcma_size == 0 and adpcmb_size == 0:
        print("\nNo ADPCM data for this game — skipping .vroma")
        return

    # ADPCM-A
    if adpcma_size > 0:
        vroma_files = [r for r in drv['roms']
                       if r['region'] == REGION_AUDIO_DATA_1]
        if vroma_files:
            game_dir = os.path.join(output_dir, game_name)
            os.makedirs(game_dir, exist_ok=True)
            out_path = os.path.join(game_dir, f'{game_name}.vroma')
            print(f"\n=== ADPCM-A Cache (.vroma) ===")
            print(f"Size: {adpcma_size // 1024} KB, files: {len(vroma_files)}")

            t0 = time.time()
            buf = bytearray(adpcma_size)

            for rom in vroma_files:
                print(f"  {rom['filename']} -> offset 0x{rom['dest']:08X}, "
                      f"{rom['size'] // 1024} KB")
                data = read_rom(rom['filename'], rom['crc'])
                dest = rom['dest']
                copy_len = min(len(data), rom['size'], adpcma_size - dest)
                buf[dest:dest + copy_len] = data[:copy_len]

            with open(out_path, 'wb') as f:
                f.write(buf)

            elapsed = time.time() - t0
            print(f"  Done in {elapsed:.1f}s — "
                  f"{os.path.getsize(out_path):,} bytes")

    # ADPCM-B (rare, but some games have it)
    if adpcmb_size > 0:
        vromb_files = [r for r in drv['roms']
                       if r['region'] == REGION_AUDIO_DATA_2]
        if vromb_files:
            game_dir = os.path.join(output_dir, game_name)
            os.makedirs(game_dir, exist_ok=True)
            out_path = os.path.join(game_dir, f'{game_name}.vromb')
            print(f"\n=== ADPCM-B Cache (.vromb) ===")
            print(f"Size: {adpcmb_size // 1024} KB, files: {len(vromb_files)}")

            t0 = time.time()
            buf = bytearray(adpcmb_size)

            for rom in vromb_files:
                print(f"  {rom['filename']} -> offset 0x{rom['dest']:08X}, "
                      f"{rom['size'] // 1024} KB")
                data = read_rom(rom['filename'], rom['crc'])
                dest = rom['dest']
                copy_len = min(len(data), rom['size'], adpcmb_size - dest)
                buf[dest:dest + copy_len] = data[:copy_len]

            with open(out_path, 'wb') as f:
                f.write(buf)

            elapsed = time.time() - t0
            print(f"  Done in {elapsed:.1f}s — "
                  f"{os.path.getsize(out_path):,} bytes")


def main():
    parser = argparse.ArgumentParser(
        description='Generate Neo Geo cache files (.ctile, .cusage, .vroma)')
    parser.add_argument('game', help='Game short name (e.g. blazstar, mslug)')
    parser.add_argument('--rom-dir', default=None,
                        help='Directory with game ZIP and gngeo_data.zip '
                             '(default: script directory)')
    parser.add_argument('--out-dir', default=None,
                        help='Output directory (default: same as --rom-dir)')
    args = parser.parse_args()

    script_dir = os.path.dirname(os.path.abspath(__file__))
    rom_dir = args.rom_dir or script_dir
    out_dir = args.out_dir or rom_dir

    game_name = args.game
    game_zip_path = os.path.join(rom_dir, f'{game_name}.zip')
    gngeo_data_path = os.path.join(rom_dir, 'gngeo_data.zip')

    # Validate inputs
    if not os.path.isfile(game_zip_path):
        print(f"ERROR: Game ZIP not found: {game_zip_path}")
        sys.exit(1)
    if not os.path.isfile(gngeo_data_path):
        print(f"ERROR: gngeo_data.zip not found: {gngeo_data_path}")
        sys.exit(1)
    os.makedirs(out_dir, exist_ok=True)

    # Load driver
    with zipfile.ZipFile(gngeo_data_path) as gz:
        try:
            drv_data = gz.read(f'rom/{game_name}.drv')
        except KeyError:
            print(f"ERROR: No driver found for '{game_name}' in gngeo_data.zip")
            print(f"Available games:")
            games = sorted(n.replace('rom/', '').replace('.drv', '')
                           for n in gz.namelist()
                           if n.startswith('rom/') and n.endswith('.drv'))
            for g in games:
                print(f"  {g}")
            sys.exit(1)

    drv = read_drv(drv_data)

    print(f"Game:    {drv['longname']} ({game_name})")
    print(f"Year:    {drv['year']}")
    print(f"Parent:  {drv['parent'] or '(none)'}")
    print(f"Sprites: {drv['romsizes'][REGION_SPRITES] // (1024*1024)} MB")
    print(f"ADPCM-A: {drv['romsizes'][REGION_AUDIO_DATA_1] // 1024} KB")
    if drv['romsizes'][REGION_AUDIO_DATA_2] > 0:
        print(f"ADPCM-B: {drv['romsizes'][REGION_AUDIO_DATA_2] // 1024} KB")
    print(f"ROM dir: {rom_dir}")
    print(f"Output:  {out_dir}")

    # Open ZIPs
    game_zip = zipfile.ZipFile(game_zip_path)
    parent_zip = None
    if drv['parent']:
        parent_path = os.path.join(rom_dir, drv['parent'] + '.zip')
        if os.path.isfile(parent_path):
            parent_zip = zipfile.ZipFile(parent_path)

    read_rom = make_rom_reader(game_zip, parent_zip)

    t_start = time.time()

    # Generate all cache files
    generate_ctile(drv, read_rom, out_dir, game_name, gngeo_data_path)
    generate_vroma(drv, read_rom, out_dir, game_name)

    # Cleanup
    game_zip.close()
    if parent_zip:
        parent_zip.close()

    t_total = time.time() - t_start
    print(f"\n{'='*50}")
    print(f"All cache files generated in {t_total:.1f}s")
    print(f"\nUpload to device:")
    game_subdir = os.path.join(out_dir, game_name)
    ctile_path = os.path.join(game_subdir, f'{game_name}.ctile')
    cusage_path = os.path.join(game_subdir, f'{game_name}.cusage')
    vroma_path = os.path.join(game_subdir, f'{game_name}.vroma')
    if os.path.isfile(ctile_path):
        print(f"  python tools/upload_papp.py {game_name}/{game_name}.cusage "
              f"--dest /sd/roms/neogeo/{game_name}/{game_name}.cusage")
        print(f"  python tools/upload_papp.py {game_name}/{game_name}.ctile "
              f"--dest /sd/roms/neogeo/{game_name}/{game_name}.ctile")
    if os.path.isfile(vroma_path):
        print(f"  python tools/upload_papp.py {game_name}/{game_name}.vroma "
              f"--dest /sd/roms/neogeo/{game_name}/{game_name}.vroma")


if __name__ == '__main__':
    main()
