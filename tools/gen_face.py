"""把参考立绘转成 128x64 单色帧: 三档量化(白/50%棋盘/黑) + 去除白底。
输出 src/anim_whale.h (用于 oled_whale.c) 与 out/face_preview.png。
"""
import os
from collections import deque
from PIL import Image

W, H = 128, 64
SPRITE_H = 60
CROP = (185, 168, 415, 405)      # 参考图中的头肩部分(去掉对话框与气泡)
LO, HI = 85, 145                 # 三档阈值


def load_sprite():
    src = Image.open(os.path.join(os.path.dirname(__file__), "..", "assets", "whale_ref2.png")).convert("L")
    im = src.crop(CROP)
    h = SPRITE_H
    w = int(im.size[0] * h / im.size[1])
    im = im.resize((w, h), Image.LANCZOS)
    px = im.load()

    # 从四角泛洪, 把接近白的背景压黑, 否则整块白
    seen = [[False] * w for _ in range(h)]
    dq = deque()
    for x in range(w):
        for y in (0, h - 1):
            if px[x, y] > 228:
                dq.append((x, y)); seen[y][x] = True
    for y in range(h):
        for x in (0, w - 1):
            if px[x, y] > 228 and not seen[y][x]:
                dq.append((x, y)); seen[y][x] = True
    while dq:
        x, y = dq.popleft(); px[x, y] = 0
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nx, ny = x + dx, y + dy
            if 0 <= nx < w and 0 <= ny < h and not seen[ny][nx] and px[nx, ny] > 228:
                seen[ny][nx] = True; dq.append((nx, ny))
    return im


def compose(sprite, dy):
    c = Image.new("1", (W, H), 0)
    c.paste(sprite, ((W - sprite.size[0]) // 2, 2 + dy))
    return c


def three_level(img):
    out = Image.new("1", img.size, 0)
    p = img.load(); q = out.load()
    for y in range(img.size[1]):
        for x in range(img.size[0]):
            v = p[x, y]
            if v > HI:
                q[x, y] = 255
            elif v > LO:
                q[x, y] = 255 if ((x + y) & 1) == 0 else 0
            else:
                q[x, y] = 0
    return out


def to_bytes(img):
    px = img.load()
    out = bytearray(W * 8)
    for page in range(8):
        for col in range(W):
            b = 0
            for bit in range(8):
                if px[col, page * 8 + bit]:
                    b |= 1 << bit
            out[page * W + col] = b
    return bytes(out)


def main():
    here = os.path.dirname(__file__)
    sprite = three_level(load_sprite())
    frames = [compose(sprite, dy) for dy in (0, 1, 0)]

    sheet = Image.new("RGB", (len(frames) * (W * 4 + 8) + 8, H * 4 + 16), (20, 20, 20))
    for i, f in enumerate(frames):
        sheet.paste(f.convert("RGB").resize((W * 4, H * 4), Image.NEAREST), (8 + i * (W * 4 + 8), 8))
    sheet.save(os.path.join(here, "..", "out", "face_preview.png"))

    data = [to_bytes(f) for f in frames]
    with open(os.path.join(here, "..", "src", "anim_whale.h"), "w", encoding="utf-8") as fh:
        fh.write("/* 自动生成: 参考立绘转 1-bit, %d 帧 128x64 */\n" % len(frames))
        fh.write("#ifndef ANIM_WHALE_H\n#define ANIM_WHALE_H\n\n")
        fh.write("#define ANIM_FRAMES %d\n" % len(frames))
        fh.write("static const unsigned char anim_whale[%d][%d] = {\n" % (len(frames), W * 8))
        for n, d in enumerate(data):
            fh.write("  { /* frame %d */\n" % n)
            for p in range(8):
                fh.write("    " + ",".join("0x%02X" % c for c in d[p * W:(p + 1) * W]) + ",\n")
            fh.write("  },\n")
        fh.write("};\n\n#endif\n")
    print("frames", len(frames), "sprite", sprite.size)


if __name__ == "__main__":
    main()
