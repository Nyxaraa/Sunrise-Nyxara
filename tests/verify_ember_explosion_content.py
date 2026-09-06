"""Check the scene wire layout and authored event keys against extracted game data.
Usage: python3 tests/verify_ember_explosion_content.py activity_sdk.pack 80BEB1CC.bin
"""
import mmap
from pathlib import Path
import struct
import sys

with Path(sys.argv[1]).open('rb') as stream, mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as data:
    start, count, stride = struct.unpack_from('<QII', data, 160 + 35 * 16)
    fields, _, field_stride = struct.unpack_from('<QII', data, 160 + 36 * 16)
    schemas = {}
    for index in range(count):
        row = struct.unpack_from('<8IQII', data, start + index * stride)
        if row[0] in (0x8080626B, 0x80807ED9, 0x808094F3, 0x808094E1, 0x808094DF):
            schemas[row[0]] = [struct.unpack_from('<6Iq6I', data, fields + i * field_stride)
                               for i in range(row[6], row[6] + row[7])]
    def layout(schema):
        return [(f[2], f[4], f[5], f[7]) for f in schemas[schema]]
    assert layout(0x8080626B) == [(0, 5, 0xFFFFFFFF, 32), (4, 2, 0xFFFFFFFF, 1),
                                 (8, 1, 0x80807ED9, 0xFFFFFFFF), (80, 1, 0x808094E1, 0xFFFFFFFF)]
    assert schemas[0x8080626B][0][6] == -2147483648
    assert layout(0x80807ED9) == [(0, 1, 0x808094F3, 0xFFFFFFFF), (68, 5, 0xFFFFFFFF, 31)]
    assert schemas[0x808094F3][0][7] == 4
    assert layout(0x808094E1) == [(0, 5, 0xFFFFFFFF, 6), (4, 1, 0x808094DF, 0)]
    assert layout(0x808094DF) == [(4, 9, 0xFFFFFFFF, 32)]

scene = Path(sys.argv[2]).read_bytes()
for suffix, offset, key in [('a', 0x5BC0, 0x329EB106), ('b', 0x5C20, 0x633B82E9),
                            ('c', 0x5C80, 0x15A78938), ('d', 0x5CE0, 0xF9D55A83)]:
    value = 0x811C9DC5
    for byte in f'explosion_set_{suffix}_trigger'.encode():
        value = ((value * 0x01000193) & 0xFFFFFFFF) ^ byte
    assert value == key
    assert struct.unpack_from('<I', scene, offset - 12)[0] == 0x8080637D
    assert struct.unpack_from('<I', scene, offset)[0] == key
print('Type-43 scene schema (74 + 32*n bits) and all four authored explosion event keys verified.')
