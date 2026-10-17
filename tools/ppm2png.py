import sys, zlib, struct
def conv(src, dst, scale=2):
    d = open(src, 'rb').read()
    parts = d.split(b'\n', 3)
    w, h = map(int, parts[1].split()); px = parts[3]
    rows = []
    for y in range(h):
        row = px[y*w*3:(y+1)*w*3]
        row = b''.join(row[x*3:x*3+3]*scale for x in range(w))
        rows += [b'\0' + row] * scale
    raw = b''.join(rows)
    def chunk(t, b): return struct.pack('>I', len(b)) + t + b + struct.pack('>I', zlib.crc32(t + b) & 0xffffffff)
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w*scale, h*scale, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b'')
    open(dst, 'wb').write(png)
for a in sys.argv[1:]: conv(a, a.rsplit('.', 1)[0] + '.png')
