#!/usr/bin/env python3
"""Compare two screenshots pixel by pixel.
Usage: imgdiff.py <original.png> <port.png> [side-by-side.png]
Prints the count of differing pixels and their bounding box; optionally writes the
two images and a heat map (differing pixels in red) side by side."""
import sys
from PIL import Image, ImageChops

a = Image.open(sys.argv[1]).convert('RGB')
b = Image.open(sys.argv[2]).convert('RGB')
if a.size != b.size:
    print(f'SIZE {a.size} vs {b.size}')
    sys.exit(1)
w, h = a.size
diff = ImageChops.difference(a, b)
px = diff.load()
changed = [(x, y) for y in range(h) for x in range(w) if px[x, y] != (0, 0, 0)]
print(f'{len(changed)} of {w * h} pixels differ ({100 * len(changed) / (w * h):.3f}%), box {diff.getbbox()}')
if len(sys.argv) > 3:
    heat = Image.blend(a, Image.new('RGB', a.size), 0.65)
    hp = heat.load()
    for x, y in changed:
        hp[x, y] = (255, 0, 0)
    side = Image.new('RGB', (w * 3 + 20, h), (40, 40, 40))
    side.paste(a, (0, 0))
    side.paste(b, (w + 10, 0))
    side.paste(heat, (2 * w + 20, 0))
    side.save(sys.argv[3])
