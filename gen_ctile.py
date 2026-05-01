#!/usr/bin/env python3
"""
Generate .ctile, .cusage, and .vroma cache files for Neo Geo sprite/ADPCM ROMs.

Reads the .drv definition from gngeo_data.zip, extracts C ROM pairs from
the game ZIP, interleaves them, optionally applies CMC42/CMC50 GFX decryption,
runs the convert_roms_tile algorithm (matching roms.c exactly), and outputs
ready-to-use cache files for the ESP32 emulator.

Usage: python gen_ctile.py <game.zip> [output_dir] [--datafile path/to/gngeo_data.zip]
  output_dir defaults to same directory as the ZIP file.
  --datafile defaults to SDcard/roms/neogeo/gngeo_data.zip relative to script dir.
"""

import sys
import os
import struct
import zipfile
import time

# Must match firmware: roms.c line 948
CUSAGE_MAGIC = 0x43553039  # "CU09"

# TILE_INVISIBLE = 1 (from transpack.h enum)
TILE_INVISIBLE = 1

# Region constants from roms.h
REGION_AUDIO_DATA_1 = 3
REGION_AUDIO_DATA_2 = 4
REGION_FIXED_LAYER_CARTRIDGE = 6
REGION_SPRITES = 9

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


# ---------------------------------------------------------------------------
# DRV file parser
# ---------------------------------------------------------------------------

def parse_drv(drv_data):
    """Parse a .drv binary file from gngeo_data.zip."""
    off = 0
    name = drv_data[off:off+32].split(b'\0')[0].decode('ascii', 'replace'); off += 32
    parent = drv_data[off:off+32].split(b'\0')[0].decode('ascii', 'replace'); off += 32
    _longname = drv_data[off:off+128]; off += 128
    year = struct.unpack_from('<I', drv_data, off)[0]; off += 4
    romsize = [struct.unpack_from('<I', drv_data, off + i*4)[0] for i in range(10)]
    off += 40
    nb = struct.unpack_from('<I', drv_data, off)[0]; off += 4

    roms = []
    for _ in range(nb):
        fn = drv_data[off:off+32].split(b'\0')[0].decode('ascii', 'replace'); off += 32
        region = drv_data[off]; off += 1
        src = struct.unpack_from('<I', drv_data, off)[0]; off += 4
        dest = struct.unpack_from('<I', drv_data, off)[0]; off += 4
        size = struct.unpack_from('<I', drv_data, off)[0]; off += 4
        crc = struct.unpack_from('<I', drv_data, off)[0]; off += 4
        roms.append(dict(filename=fn, region=region, src=src, dest=dest, size=size, crc=crc))

    return name, parent, romsize, roms


def get_crom_pairs(romfiles):
    """Extract C ROM pairs from romfile list."""
    sprites = [r for r in romfiles if r['region'] == REGION_SPRITES]
    evens = sorted([r for r in sprites if (r['dest'] & 1) == 0], key=lambda r: r['dest'])
    odds  = sorted([r for r in sprites if (r['dest'] & 1) == 1], key=lambda r: r['dest'])

    pairs = []
    for e, o in zip(evens, odds):
        assert e['size'] == o['size'], f"Mismatched pair sizes: {e['filename']} vs {o['filename']}"
        pairs.append((e['filename'], o['filename'], e['dest'] & ~1, e['size']))

    return pairs


def get_adpcm_roms(romfiles, region):
    """Get ADPCM ROM entries sorted by dest."""
    return sorted([r for r in romfiles if r['region'] == region], key=lambda r: r['dest'])


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


# ---------------------------------------------------------------------------
# Tile conversion
# ---------------------------------------------------------------------------

def convert_roms_tile(tile_data):
    """Convert one 128-byte raw tile to decoded 4bpp format.
    Exactly matches convert_roms_tile() in roms.c."""

    swap = bytearray(tile_data)
    out = bytearray(128)
    usage = 0
    out_idx = 0

    for y in range(16):
        dw = 0
        for x in range(8):
            pen  = ((swap[64 + (y << 2) + 3] >> x) & 1) << 3
            pen |= ((swap[64 + (y << 2) + 1] >> x) & 1) << 2
            pen |= ((swap[64 + (y << 2) + 2] >> x) & 1) << 1
            pen |= (swap[64 + (y << 2)] >> x) & 1
            dw |= pen << ((7 - x) << 2)
            usage |= (1 << pen)
        struct.pack_into('<I', out, out_idx, dw)
        out_idx += 4

        dw = 0
        for x in range(8):
            pen  = ((swap[(y << 2) + 3] >> x) & 1) << 3
            pen |= ((swap[(y << 2) + 1] >> x) & 1) << 2
            pen |= ((swap[(y << 2) + 2] >> x) & 1) << 1
            pen |= (swap[(y << 2)] >> x) & 1
            dw |= pen << ((7 - x) << 2)
            usage |= (1 << pen)
        struct.pack_into('<I', out, out_idx, dw)
        out_idx += 4

    return bytes(out), usage


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <game.zip> [output_dir] [--datafile gngeo_data.zip]")
        sys.exit(1)

    args = sys.argv[1:]
    datafile = None
    positional = []
    i = 0
    while i < len(args):
        if args[i] == '--datafile' and i + 1 < len(args):
            datafile = args[i + 1]
            i += 2
        else:
            positional.append(args[i])
            i += 1

    zip_path = positional[0]
    output_dir = positional[1] if len(positional) > 1 else os.path.dirname(os.path.abspath(zip_path))

    if datafile is None:
        script_dir = os.path.dirname(os.path.abspath(__file__))
        datafile = os.path.join(script_dir, 'SDcard', 'roms', 'neogeo', 'gngeo_data.zip')

    game_name = os.path.splitext(os.path.basename(zip_path))[0]
    game_dir = os.path.join(output_dir, game_name)
    os.makedirs(game_dir, exist_ok=True)
    ctile_path = os.path.join(game_dir, f"{game_name}.ctile")
    cusage_path = os.path.join(game_dir, f"{game_name}.cusage")
    vroma_path = os.path.join(game_dir, f"{game_name}.vroma")

    # Load driver definition
    print(f"Game: {game_name}")
    print(f"ZIP:  {zip_path}")
    print(f"Datafile: {datafile}")

    with zipfile.ZipFile(datafile, 'r') as dzf:
        drv_data = dzf.read(f'rom/{game_name}.drv')
    drv_name, drv_parent, romsize, romfiles = parse_drv(drv_data)
    print(f"Driver: {drv_name} (parent: {drv_parent})")

    crom_pairs = get_crom_pairs(romfiles)
    total_size = romsize[REGION_SPRITES]
    print(f"Sprite ROM size: {total_size // (1024*1024)} MB ({len(crom_pairs)} pairs)")
    for even, odd, base, size in crom_pairs:
        print(f"  {even} + {odd} -> 0x{base:07X} ({size // (1024*1024)} MB each)")

    encrypt_type = None
    extra_xor = None
    if game_name in CMC42_GAMES:
        encrypt_type = 'cmc42'
        extra_xor = CMC42_GAMES[game_name]
        print(f"Encryption: CMC42 (extra_xor=0x{extra_xor:02x})")
    elif game_name in CMC50_GAMES:
        encrypt_type = 'cmc50'
        extra_xor = CMC50_GAMES[game_name]
        print(f"Encryption: CMC50 (extra_xor=0x{extra_xor:02x})")
    else:
        print("Encryption: none")

    print(f"Output: {ctile_path}")
    print(f"Allocating {total_size // (1024*1024)} MB buffer...")
    tiles_buf = bytearray(total_size)

    t0 = time.time()

    # Open ZIPs for ROM reading
    game_zip = zipfile.ZipFile(zip_path, 'r')
    parent_zip = None
    if drv_parent and drv_parent != 'neogeo':
        parent_path = os.path.join(os.path.dirname(os.path.abspath(zip_path)), f"{drv_parent}.zip")
        if os.path.exists(parent_path):
            parent_zip = zipfile.ZipFile(parent_path, 'r')

    def read_rom(filename):
        """Read a ROM file from game ZIP or parent ZIP (with basename fallback)."""
        for zf in [game_zip, parent_zip]:
            if zf is None:
                continue
            try:
                return zf.read(filename)
            except KeyError:
                pass
            fl = filename.lower()
            base = os.path.splitext(fl)[0]
            for n in zf.namelist():
                if n.lower() == fl:
                    return zf.read(n)
                if os.path.splitext(n)[0].lower() == base:
                    print(f"    (matched {filename} -> {n})")
                    return zf.read(n)
        raise FileNotFoundError(f"ROM file not found: {filename}")

    # Interleave C ROM pairs
    for even_name, odd_name, dest_base, rom_size in crom_pairs:
        print(f"  Interleaving {even_name} + {odd_name} -> offset 0x{dest_base:07X}")
        even_data = read_rom(even_name)
        odd_data = read_rom(odd_name)
        assert len(even_data) == rom_size, f"{even_name}: expected {rom_size}, got {len(even_data)}"
        assert len(odd_data) == rom_size, f"{odd_name}: expected {rom_size}, got {len(odd_data)}"
        for i in range(rom_size):
            tiles_buf[dest_base + i * 2] = even_data[i]
            tiles_buf[dest_base + i * 2 + 1] = odd_data[i]

    t1 = time.time()
    print(f"Interleave done in {t1-t0:.1f}s")

    # Apply GFX decryption if needed
    if encrypt_type is not None:
        table_file = f"{encrypt_type}.xor"
        print(f"Loading {table_file} from {datafile}...")
        tables = load_xor_tables(datafile, table_file)
        print(f"Decrypting ({encrypt_type}, extra_xor=0x{extra_xor:02x})...")
        neogeo_gfx_decrypt(tiles_buf, total_size, extra_xor, tables)
        t1b = time.time()
        print(f"Decryption done in {t1b-t1:.1f}s")
        t1 = t1b

        # Extract SFIX (fix layer) from end of decrypted C ROM
        # neogeo_sfix_decrypt() in neocrypt.c does this but is skipped in streaming mode
        tx_size = romsize[REGION_FIXED_LAYER_CARTRIDGE]
        if tx_size > 0 and tx_size <= total_size:
            print(f"Extracting SFIX ({tx_size // 1024} KB) from end of decrypted C ROM...")
            sfix_data = neogeo_sfix_extract(tiles_buf, total_size, tx_size)
            sfix_path = os.path.join(game_dir, f"{game_name}.sfix")
            with open(sfix_path, 'wb') as f:
                f.write(sfix_data)
            print(f"  {sfix_path}: {len(sfix_data):,} bytes")
        else:
            print(f"  SFIX: no fixed region in driver (size={tx_size})")

    # Convert tiles and build spr_usage
    nb_tiles = total_size >> 7
    nb_usage = nb_tiles >> 4
    spr_usage = [0] * nb_usage

    print(f"Converting {nb_tiles} tiles...")
    converted_buf = bytearray(total_size)
    tiles_with_data = 0

    for tile in range(nb_tiles):
        offset = tile << 7
        raw = tiles_buf[offset:offset + 128]
        conv, usage = convert_roms_tile(raw)
        converted_buf[offset:offset + 128] = conv

        if (usage & ~1) == 0:
            spr_usage[tile >> 4] |= (TILE_INVISIBLE << ((tile & 0xF) * 2))
        else:
            tiles_with_data += 1

        if tile > 0 and tile % 100000 == 0:
            print(f"  {tile}/{nb_tiles} ({100*tile//nb_tiles}%)")

    t2 = time.time()
    print(f"Conversion done in {t2-t1:.1f}s  ({tiles_with_data} visible tiles)")

    # Write .ctile
    print(f"Writing {ctile_path} ({total_size // (1024*1024)} MB)...")
    with open(ctile_path, 'wb') as f:
        f.write(converted_buf)

    # Write .cusage
    usage_bytes = struct.pack('<I', CUSAGE_MAGIC)
    for u in spr_usage:
        usage_bytes += struct.pack('<I', u)
    print(f"Writing {cusage_path} ({len(usage_bytes)} bytes)...")
    with open(cusage_path, 'wb') as f:
        f.write(usage_bytes)

    # Generate .vroma
    adpcm_roms = get_adpcm_roms(romfiles, REGION_AUDIO_DATA_1)
    if adpcm_roms:
        adpcm_total = romsize[REGION_AUDIO_DATA_1]
        print(f"Generating ADPCM-A cache: {adpcm_total // (1024*1024)} MB...")
        vroma_buf = bytearray(adpcm_total)
        for r in adpcm_roms:
            print(f"  {r['filename']} -> offset 0x{r['dest']:07X} ({r['size'] // 1024} KB)")
            data = read_rom(r['filename'])
            assert len(data) == r['size'], f"{r['filename']}: expected {r['size']}, got {len(data)}"
            vroma_buf[r['dest']:r['dest'] + r['size']] = data
        print(f"Writing {vroma_path}...")
        with open(vroma_path, 'wb') as f:
            f.write(vroma_buf)

    game_zip.close()
    if parent_zip:
        parent_zip.close()

    t3 = time.time()
    print(f"\nDone! Total time: {t3-t0:.1f}s")
    print(f"  {ctile_path}: {os.path.getsize(ctile_path):,} bytes")
    print(f"  {cusage_path}: {os.path.getsize(cusage_path):,} bytes")
    if adpcm_roms:
        print(f"  {vroma_path}: {os.path.getsize(vroma_path):,} bytes")


if __name__ == '__main__':
    main()
