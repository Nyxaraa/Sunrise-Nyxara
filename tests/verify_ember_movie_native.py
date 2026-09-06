"""Offline ABI verification against a mapped/decrypted image, never an on-disk encrypted EXE.
Usage: python3 tests/verify_ember_movie_native.py path/to/game_image.bin [packages-directory]
"""
import re
import struct
import sys
from pathlib import Path

repo = Path(__file__).resolve().parents[1]
data = Path(sys.argv[1]).read_bytes()

def signature(file, name):
    source = (repo / file).read_text()
    pattern = re.search(r'constexpr auto ' + name + r'\s*=\s*signature<signature_length\("([^"]+)"\)', source)[1]
    regex = b''.join(b'.' if token == '?' else re.escape(bytes([int(token, 16)])) for token in pattern.split())
    matches = [m.start() for m in re.finditer(regex, data, re.S)]
    assert len(matches) == 1, (name, matches)
    return matches[0]

def target(base, offset, expected):
    assert data[base + offset] == 0xE8
    value = base + offset + 5 + struct.unpack_from('<i', data, base + offset + 1)[0]
    assert value == expected, (hex(base), hex(offset), hex(value), hex(expected))

path = 'Sunrise/src/client/hooks/ember_movies/resources.cpp'
orbit = 'Sunrise/src/client/hooks/ember_movies/orbit_return.cpp'
return_path = signature(orbit, 'returnSig')
assert return_path == 0xE19D50
for offset, expected in [(0x1E,0xB48550),(0x2F,0xE35820),(0x5B,0xBF84B0),
                         (0x65,0xC06330),(0x6A,0xBF95D0),(0x7E,0xBFB1F0),(0x86,0xBF97D0)]:
    target(return_path,offset,expected)
assert signature(orbit,'activitySig') == 0xC294B0
assert signature(orbit,'lifetimeSig') == 0x4FFD10
assert signature(orbit,'stepSig') == 0xE1B4D0
target(0xE1D619,0,0xE35820)
target(0xE1D619,0x15,0xE1B4D0)
assert data[0xE1D626:0xE1D62B] == bytes.fromhex('ba 1c 00 00 00')
# Native UI return selection: initialize, construct default orbit, clear/select/commit.
for site, expected in [(0x157727F,0xBF84B0),(0x157728C,0xC06330),
                       (0x1577291,0xBF95D0),(0x15772A8,0xBFB1F0),(0x15772B4,0xBF97D0)]:
    target(site,0,expected)
assert data[0xBF84E4:0xBF84EB] == bytes.fromhex('c6 83 18 01 00 00 00')
print('Native orbit selection/commit, lifetime reader and deferred cleanup ABI verified.')
load = signature(path, 'loadSig')
end = signature(path, 'endSig')
assert load == 0xB46E10 and end == 0xB44020
surface = signature(path, 'surfaceSig')
assert surface == 0x1202B00
assert surface + 13 + struct.unpack_from('<i', data, surface + 9)[0] == 0x2E800B0
target(0xB5F4C5, 9, surface)  # native world activation publishes the surface registration stacks
target(0x1184660, 0x37, 0x1202C20)  # renderer fetches selected surface definitions
assert data[0x116A070:0x116A077] == bytes.fromhex('48 83 38 00 0f 95 c0')  # missing surface => skip GPU upload
# Captured a47adbd exception: C0000005, RIP 1204163, RAX=0, RBX=80BCA021,
# RDX=4. The registration callback reads the unloaded definition's slot directly.
assert data[0x1204160:0x1204167] == bytes.fromhex('48 2b c1 48 0f be 08')
# Native type-19 definitions need 16 bytes, not their eight-byte package size.
assert struct.unpack_from('<I',data,0x1202488+18*4)[0] == 0x1202478
assert data[0x1202478:0x120247F] == bytes.fromhex('ba 10 00 00 00 8b c2')
# The type-19 raw buffer callback fills definition+8; the renderer reads that pointer.
assert struct.unpack_from('<I',data,0x12045D0+18*4)[0] == 0x1204581
assert data[0x12045BF:0x12045C6] == bytes.fromhex('48 2b c1 4c 89 40 08')
assert data[0x4A6340:0x4A6345] == bytes.fromhex('48 8b 41 08 c3')
ui = signature('Sunrise/src/client/hooks/bootflow/ember_movie_ui.cpp', 'sig')
assert ui == 0x132BD80
target(0x132B890, 0x353, 0x1278FF0)  # native movie command is queued before either UI layer
target(0x132B890, 0x3DF, ui)
target(0x132B890, 0x40C, ui)
for offset, expected in [(0x96, 0x4294D0), (0xD1, 0x423EF0), (0x14C, 0x4312D0), (0x157, 0x435AA0)]:
    target(load, offset, expected)
for offset, expected in [(0x85, 0x42C650), (0x9F, 0x425310)]:
    target(end, offset, expected)
assert data[end + 0x2E:end + 0x31] == bytes.fromhex('48 8B 05')
assert end + 0x35 + struct.unpack_from('<i', data, end + 0x31)[0] == 0x2439C70
# Native tag classifier, including the semantic distinction missed by the old test:
# ordinary tag -> kind 1; shared type-16 tag (type_info & F000 == 2000) -> kind 2.
assert data[0x42694F:0x42696F] == bytes.fromhex(
    '8b 45 04 8b cb 48 89 7c 24 30 25 00 f0 00 00 33 ff 3d 00 20 00 00 40 0f 94 c7 45 33 c0 8d 57 01')
target(0x426920, 0x4F, 0x433050)
# Kind 2 is routed to root+10; ordinary metadata belongs in root+20.
assert data[0x4313CD:0x4313E2] == bytes.fromhex(
    '83 3f 02 b9 10 00 00 00 8b 57 04 41 b8 20 00 00 00 44 0f 44 c1')
# For stream type_info & 30000 == 10000, the load job maps offset|patch and
# size|C0000000 directly. It bypasses the ordinary allocation/read branch.
assert data[0x3592C6:0x3592EB] == bytes.fromhex(
    '8b c3 c1 e8 10 83 e0 03 83 f8 01 75 2f 41 0f b7 4d 20 41 81 cf 00 00 00 c0 8b 55 50 45 8b c7 48 0b d1 8b 4d 48')
target(0x3591B0, 0x13B, 0x351D00)
target(0x41A160, 0x16, 0x3597C0)  # native video I/O opens this mapped package/patch
target(0x41A160, 0x2C, 0x357DA0)  # then obtains offset and byte length
movie = 'Sunrise/src/client/hooks/ember_movies/ember_movies.cpp'
start, stop, busy = (signature(movie, name) for name in ('startSig', 'stopSig', 'busySig'))
for offset, expected in [(0x72, 0x41B040), (0x7A, 0x41A3C0), (0x8E, 0x41CD20)]:
    target(start, offset, expected)
for offset, expected in [(0x18, 0x41D0C0), (0x25, 0x41A980)]:
    target(stop, offset, expected)
assert busy == 0x41B420
target(busy, 0x48, 0x41AB70)

if len(sys.argv) > 2:
    # Read only container metadata, without unpacking data or starting the game.
    latest = {}
    for path in Path(sys.argv[2]).glob('*.pkg'):
        with path.open('rb') as stream:
            header = stream.read(0x170)
        package = struct.unpack_from('<H', header, 4)[0]
        version = (struct.unpack_from('<Q', header, 0x10)[0],
                   struct.unpack_from('<I', header, 0x1C)[0],
                   struct.unpack_from('<H', header, 0x20)[0])
        if package not in latest or version > latest[package][0]:
            latest[package] = version, path, header
    for tag, expected in [(0x80BCA001, 0x80808495), (0x80BCA003, 0x80808495),
                          (0x80BCA000, 0x80808499), (0x80BCA002, 0x80808499),
                          (0x80B9EB33, 0x80809A88), (0x80B9EB34, 0x80809A88),
                          (0x80BCA032, 0x80806B8F),
                          (0x80BCA022, 0x80806B91), (0x80BCA025, 0x80806B91),
                          (0x80BCA028, 0x80806B91), (0x80BCA02B, 0x80806B91),
                          (0x80BCA02E, 0x80806B91), (0x80BCA031, 0x80806B91),
                          (0x80BCA021, 0x80BCA020), (0x80BCA024, 0x80BCA023),
                          (0x80BCA026, 0x80BCA027), (0x80BCA029, 0x80BCA02A),
                          (0x80BCA02C, 0x80BCA02D), (0x80BCA02F, 0x80BCA030),
                          (0x80BCA020, 0x80BCA021), (0x80BCA023, 0x80BCA024),
                          (0x80BCA027, 0x80BCA026), (0x80BCA02A, 0x80BCA029),
                          (0x80BCA02D, 0x80BCA02C), (0x80BCA030, 0x80BCA02F),
                          (0x80BCA034, 0xFFFFFFFF), (0x80C7C000, 0xFFFFFFFF)]:
        # Tag package IDs include the bank: 80BCAxxx belongs to package 01E5.
        package = (tag >> 13) & 0x3FF
        _, path, header = latest[package]
        table = (struct.unpack_from('<I', header, 0x110)[0] + 0x60 if header[0x1A] == 1
                 else struct.unpack_from('<I', header, 0xB8)[0])
        with path.open('rb') as stream:
            stream.seek(table + (tag & 0x1FFF) * 16)
            reference, type_info, _ = struct.unpack('<IIQ', stream.read(16))
        assert reference == expected, (hex(tag), hex(reference), path)
        assert type_info & 0xF000 != 0x2000, (hex(tag), hex(type_info))
        if expected == 0xFFFFFFFF:
            assert (type_info & 0x30000) == 0x10000 and (type_info >> 6) & 0x3F == 24
        if 0x80BCA020 <= expected <= 0x80BCA030:
            definitions = {0x80BCA021,0x80BCA024,0x80BCA026,0x80BCA029,0x80BCA02C,0x80BCA02F}
            assert (type_info & 0x3FFFF) == (0x44FB if tag in definitions else 0x254FB)
    print('Movie metadata, streams, surface containers, definitions and raw buffers all use kind 1.')
    tags = repo / 'build/first-encounter-audit/tags'
    catalog = (tags / '80BCA032.bin').read_bytes()
    for i, (container, definition) in enumerate(zip(
        [0x80BCA022,0x80BCA025,0x80BCA028,0x80BCA02B,0x80BCA02E,0x80BCA031],
        [0x80BCA021,0x80BCA024,0x80BCA026,0x80BCA029,0x80BCA02C,0x80BCA02F])):
        assert struct.unpack_from('<I', catalog, 0x38 + 16*i)[0] == container
        assert (tags / f'{container:08X}.bin').read_bytes() == struct.pack('<I', definition)
        assert (tags / f'{definition:08X}.bin').read_bytes()[0] == i + 1
    print('Six authored Y/U/V definitions map to the renderer slots 1..6.')
attach = signature('Sunrise/src/client/hooks/bootflow/ember_sunburn.cpp', 'sig')
assert attach == 0x9F2760
# Native attach dereferences the runtime relative template, then passes it to the child factory.
target(attach, 0x68, 0x32BBD0)
target(attach, 0x78, 0x56DE00)
assert data[0x4AE000:0x4AE002] == bytes.fromhex('8B 09')  # factory reads resource at request+0
print('Native resource kind selection, request lifecycle, movie playback and sunburn attachment ABI verified.')
