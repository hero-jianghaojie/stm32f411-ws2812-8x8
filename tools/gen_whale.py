"""生成"鲸鱼娘"128x64 单色动画帧, 输出 C 头文件 + 预览图。

帧格式与 oled_test.c 的帧缓冲一致: fb[page*128 + col], bit0 = 该页最上面一行。
"""
import os
from PIL import Image, ImageDraw

W, H, PAGES = 128, 64, 8
NFRAMES = 8
OUT_H = os.path.join(os.path.dirname(__file__), "..", "src", "anim_whale.h")
OUT_PNG = os.path.join(os.path.dirname(__file__), "..", "out", "whale_preview.png")
OUT_GIF = os.path.join(os.path.dirname(__file__), "..", "out", "whale_anim.gif")


def circle(d, cx, cy, r, fill=255):
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=fill)


def outlined_ellipse(d, box, gap=1, fill=255):
    d.ellipse(box, fill=0)
    x0, y0, x1, y1 = box
    d.ellipse([x0 + gap, y0 + gap, x1 - gap, y1 - gap], fill=fill)


def draw_frame(i):
    img = Image.new("L", (W, H), 0)
    d = ImageDraw.Draw(img)

    bob = [0, -1, -2, -1, 0, 1, 2, 1][i % 8]
    blink = (i % 8) == 3
    cx, cy = 56, 24 + bob

    # 尾巴(右侧), 摆动: 先黑后白, 与身体之间留出黑缝
    wag = [-4, -2, 1, 3, 2, 0, -3, -6][i % 8]
    d.polygon([(80, 40 + bob), (100, 30 + bob + wag), (100, 50 + bob + wag), (80, 46 + bob)], fill=0)
    d.polygon([(82, 41 + bob), (97, 33 + bob + wag), (97, 47 + bob + wag), (82, 45 + bob)], fill=255)

    # 身体
    outlined_ellipse(d, [34, 34 + bob, 84, 60 + bob], 1)

    # 鳍(左右), 带黑边
    outlined_ellipse(d, [24, 38 + bob, 42, 47 + bob], 1)
    outlined_ellipse(d, [76, 38 + bob, 96, 47 + bob], 1)

    # 头(压在身体上, 黑边隔开)
    outlined_ellipse(d, [cx - 15, cy - 15, cx + 15, cy + 15], 1)

    # 头发: 上半部涂黑
    d.chord([cx - 14, cy - 14, cx + 14, cy + 14], 182, 358, fill=0)
    # 刘海: 三个下垂的尖
    for dx in (-9, 0, 9):
        d.polygon([(cx + dx - 4, cy - 9), (cx + dx + 4, cy - 9), (cx + dx, cy - 2)], fill=0)
    # 两鬓长发
    d.polygon([(cx - 15, cy - 6), (cx - 18, cy + 14), (cx - 12, cy + 10)], fill=0)
    d.polygon([(cx + 15, cy - 6), (cx + 18, cy + 14), (cx + 12, cy + 10)], fill=0)

    # 脸: 眼睛/嘴/腮红(黑点 = 有像素, 在白眼区上)
    if blink:
        d.line([cx - 8, cy + 3, cx - 4, cy + 3], fill=0, width=1)
        d.line([cx + 4, cy + 3, cx + 8, cy + 3], fill=0, width=1)
    else:
        d.rectangle([cx - 8, cy + 1, cx - 5, cy + 5], fill=0)
        d.rectangle([cx + 5, cy + 1, cx + 8, cy + 5], fill=0)
        d.point((cx - 7, cy + 2), fill=255)
        d.point((cx + 6, cy + 2), fill=255)
    d.arc([cx - 3, cy + 5, cx + 3, cy + 10], 20, 160, fill=0)
    for (bx, by) in [(cx - 11, cy + 6), (cx - 10, cy + 7), (cx + 10, cy + 6), (cx + 11, cy + 7)]:
        d.point((bx, by), fill=200)

    # 头顶喷水
    for k, (dx, dy) in enumerate([(-4, -19), (0, -23), (4, -19)]):
        if (i + k) % 3 != 2:
            d.ellipse([cx + dx - 1, cy + dy - 1, cx + dx + 1, cy + dy + 1], fill=0)
            d.ellipse([cx + dx - 1, cy + dy - 1, cx + dx, cy + dy], fill=255)

    # 气泡
    for k in range(4):
        bx = 16 + k * 8
        by = 62 - ((i * 4 + k * 13) % 58)
        r = 1 + (k % 2)
        d.ellipse([bx - r, by - r, bx + r, by + r], fill=0)
        d.ellipse([bx - r + 1, by - r, bx + r - 1, by + r - 1], fill=255)

    # 底部水线
    d.line([0, 61, W - 1, 61], fill=200)

    return img.point(lambda v: 255 if v > 110 else 0, "1")


def to_bytes(img):
    """转成 8 页 × 128 字节, 每字节 bit0 = 页内最上一行。"""
    px = img.load()
    out = bytearray(W * PAGES)
    for page in range(PAGES):
        for col in range(W):
            b = 0
            for bit in range(8):
                y = page * 8 + bit
                if px[col, y]:
                    b |= (1 << bit)
            out[page * W + col] = b
    return bytes(out)


def main():
    frames = [draw_frame(i) for i in range(NFRAMES)]

    # 预览: 8 帧拼成 2x4, 每帧放大 3 倍
    scale = 3
    cols, rows = 2, 4
    pw, ph = W * scale, H * scale
    sheet = Image.new("RGB", (cols * (pw + 8) + 8, rows * (ph + 8) + 8), (20, 20, 20))
    for idx, f in enumerate(frames):
        big = f.convert("RGB").resize((pw, ph), Image.NEAREST)
        x = 8 + (idx % cols) * (pw + 8)
        y = 8 + (idx // cols) * (ph + 8)
        sheet.paste(big, (x, y))
    sheet.save(OUT_PNG)

    frames[0].save(OUT_GIF, save_all=True, append_images=frames[1:], duration=120, loop=0)

    data = [to_bytes(f) for f in frames]
    with open(OUT_H, "w", encoding="utf-8") as fh:
        fh.write("/* 自动生成: 鲸鱼娘 8 帧 128x64 单色动画, 勿手改 */\n")
        fh.write("#ifndef ANIM_WHALE_H\n#define ANIM_WHALE_H\n\n")
        fh.write("#define ANIM_FRAMES %d\n" % NFRAMES)
        fh.write("static const unsigned char anim_whale[%d][%d] = {\n" % (NFRAMES, W * PAGES))
        for n, d in enumerate(data):
            fh.write("  { /* frame %d */\n" % n)
            for p in range(PAGES):
                chunk = d[p * W:(p + 1) * W]
                fh.write("    " + ",".join("0x%02X" % c for c in chunk) + ",\n")
            fh.write("  },\n")
        fh.write("};\n\n#endif\n")
    print("frames:", NFRAMES, "bytes/frame:", W * PAGES, "->", OUT_H)
    print("preview:", OUT_PNG, " gif:", OUT_GIF)


if __name__ == "__main__":
    main()
