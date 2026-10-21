#!/usr/bin/env python3
"""Original 25px magnifying glass + diagnostic bars; stdlib PNG encoder."""
from pathlib import Path
import struct
import zlib

SIZE = 25
pixels = bytearray(SIZE * SIZE * 4)
for y in range(SIZE):
    for x in range(SIZE):
        ring = 36 <= (x - 10) ** 2 + (y - 10) ** 2 <= 81
        handle = 16 <= x <= 23 and 16 <= y <= 23 and abs(x - y) <= 2
        bars = (x in (7, 8) and 11 <= y <= 14 or
                x in (10, 11) and 8 <= y <= 14 or
                x in (13, 14) and 10 <= y <= 14)
        if ring or handle or bars:
            pixels[(y * SIZE + x) * 4:(y * SIZE + x) * 4 + 4] = b'\xff' * 4

def chunk(kind, data):
    return (struct.pack('>I', len(data)) + kind + data +
            struct.pack('>I', zlib.crc32(kind + data)))

scan = b''.join(b'\0' + pixels[y * SIZE * 4:(y + 1) * SIZE * 4] for y in range(SIZE))
png = (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', SIZE, SIZE, 8, 6, 0, 0, 0)) +
       chunk(b'IDAT', zlib.compress(scan, 9)) + chunk(b'IEND', b''))
target = Path(__file__).resolve().parents[1] / 'resources/images/inspect.png'
target.write_bytes(png)
print(target)
