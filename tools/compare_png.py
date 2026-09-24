#!/usr/bin/env python3
"""Compare two PNGs pixel by pixel.

Used to check browser-rendered output against images the original game
produced. Supports the subset the game writes and ships: non-interlaced
8-bit indexed, 24-bit RGB and 32-bit RGBA.
"""
import struct
import sys
import zlib


def read_png(path):
    data = open(path, 'rb').read()
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError(f'{path}: not a PNG')
    offset, palette, idat, header = 8, None, bytearray(), None
    while offset < len(data):
        length = struct.unpack('>I', data[offset:offset + 4])[0]
        kind = data[offset + 4:offset + 8]
        body = data[offset + 8:offset + 8 + length]
        if kind == b'IHDR':
            width, height, depth, color, _, _, interlace = struct.unpack('>IIBBBBB', body)
            if depth != 8 or interlace:
                raise ValueError(f'{path}: unsupported depth {depth} interlace {interlace}')
            header = (width, height, color)
        elif kind == b'PLTE':
            palette = body
        elif kind == b'IDAT':
            idat += body
        elif kind == b'IEND':
            break
        offset += 12 + length

    width, height, color = header
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[color]
    raw = zlib.decompress(bytes(idat))
    stride = width * channels
    out = bytearray(height * stride)
    previous = bytearray(stride)
    position = 0
    for row in range(height):
        filter_type = raw[position]
        position += 1
        line = bytearray(raw[position:position + stride])
        position += stride
        for index in range(stride):
            left = line[index - channels] if index >= channels else 0
            up = previous[index]
            upper_left = previous[index - channels] if index >= channels else 0
            if filter_type == 1:
                line[index] = (line[index] + left) & 0xFF
            elif filter_type == 2:
                line[index] = (line[index] + up) & 0xFF
            elif filter_type == 3:
                line[index] = (line[index] + ((left + up) >> 1)) & 0xFF
            elif filter_type == 4:
                estimate = left + up - upper_left
                distance_left = abs(estimate - left)
                distance_up = abs(estimate - up)
                distance_corner = abs(estimate - upper_left)
                if distance_left <= distance_up and distance_left <= distance_corner:
                    predictor = left
                elif distance_up <= distance_corner:
                    predictor = up
                else:
                    predictor = upper_left
                line[index] = (line[index] + predictor) & 0xFF
        out[row * stride:(row + 1) * stride] = line
        previous = line

    pixels = []
    for index in range(width * height):
        sample = out[index * channels:(index + 1) * channels]
        if color == 3:
            entry = sample[0] * 3
            pixels.append(tuple(palette[entry:entry + 3]))
        elif color == 2:
            pixels.append(tuple(sample))
        elif color == 6:
            pixels.append(tuple(sample[:3]))
        else:
            pixels.append((sample[0],) * 3)
    return width, height, pixels


def main():
    first, second = sys.argv[1], sys.argv[2]
    tolerance = int(sys.argv[3]) if len(sys.argv) > 3 else 0
    width_a, height_a, pixels_a = read_png(first)
    width_b, height_b, pixels_b = read_png(second)
    if (width_a, height_a) != (width_b, height_b):
        print(f'size differs: {width_a}x{height_a} vs {width_b}x{height_b}')
        return 1
    differing = 0
    worst = 0
    total = 0
    for left, right in zip(pixels_a, pixels_b):
        delta = max(abs(left[0] - right[0]), abs(left[1] - right[1]), abs(left[2] - right[2]))
        total += delta
        worst = max(worst, delta)
        if delta > tolerance:
            differing += 1
    count = width_a * height_a
    print(f'{width_a}x{height_a} pixels, {differing} differ beyond {tolerance} '
          f'({differing * 100.0 / count:.2f}%), worst channel delta {worst}, '
          f'mean channel delta {total / count:.2f}')
    return 0 if differing == 0 else 2


if __name__ == '__main__':
    sys.exit(main())
