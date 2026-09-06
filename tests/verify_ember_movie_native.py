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
load = signature(path, 'loadSig')
end = signature(path, 'endSig')
assert load == 0xB46E10 and end == 0xB44020
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
                          (0x80BCA032, 0x80806B8F)]:
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
    print('Installed movie metadata classes and ordinary (kind 1) package types verified.')
attach = signature('Sunrise/src/client/hooks/bootflow/ember_sunburn.cpp', 'sig')
assert attach == 0x9F2760
# Native attach dereferences the runtime relative template, then passes it to the child factory.
target(attach, 0x68, 0x32BBD0)
target(attach, 0x78, 0x56DE00)
assert data[0x4AE000:0x4AE002] == bytes.fromhex('8B 09')  # factory reads resource at request+0
print('Native resource kind selection, request lifecycle, movie playback and sunburn attachment ABI verified.')
