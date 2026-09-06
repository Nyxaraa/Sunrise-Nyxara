"""Offline ABI verification against a mapped/decrypted image, never an on-disk encrypted EXE.
Usage: python3 tests/verify_ember_movie_native.py path/to/game_image.bin
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
attach = signature('Sunrise/src/client/hooks/bootflow/ember_sunburn.cpp', 'sig')
assert attach == 0x9F2760
# Native attach dereferences the runtime relative template, then passes it to the child factory.
target(attach, 0x68, 0x32BBD0)
target(attach, 0x78, 0x56DE00)
assert data[0x4AE000:0x4AE002] == bytes.fromhex('8B 09')  # factory reads resource at request+0
print('Native resource request / submit / status / release and sunburn attachment ABI verified.')
