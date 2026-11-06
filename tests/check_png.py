"""Checks that a file is a well-formed PNG (every chunk's CRC, the data the size of its rows) at least 640 x 360."""
import struct
import sys
import zlib

data = open(sys.argv[1], "rb").read()
assert data[:8] == b"\x89PNG\r\n\x1a\n", "signature"
pos, idat, w, h = 8, b"", 0, 0
while pos < len(data):
    (n,) = struct.unpack(">I", data[pos:pos + 4])
    kind, body = data[pos + 4:pos + 8], data[pos + 8:pos + 8 + n]
    (crc,) = struct.unpack(">I", data[pos + 8 + n:pos + 12 + n])
    assert zlib.crc32(kind + body) & 0xFFFFFFFF == crc, "chunk crc"
    if kind == b"IHDR":
        w, h = struct.unpack(">II", body[:8])
    if kind == b"IDAT":
        idat += body
    pos += 12 + n
raw = zlib.decompress(idat)
assert len(raw) == h * (1 + 3 * w) and w >= 640 and h >= 360, "size"
