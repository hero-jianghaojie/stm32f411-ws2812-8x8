"""按参考图重绘的像素风"鲸鱼娘"：64x64 精灵, 2 帧(睁眼/眨眼), 居中放到 128x64。
风格取自参考图: 蓝色双马尾、白色蕾丝女仆发带、大眼睛带高光、深色衣领 + 蓝蝴蝶结。
"""
import os
from PIL import Image, ImageDraw

W, H = 128, 64
SP = 64                      # 精灵尺寸
OX, OY = (W - SP) // 2, 0

BLACK, WHITE = 0, 255


def frill(d, x0, x1, y):
    """沿 y 画一排小半圆, 做蕾丝边。"""
    x = x0
    while x < x1:
        d.arc([x - 2, y - 2, x + 2, y + 2], 180, 360, fill=WHITE)
        x += 4


def ring_ellipse(d, box, gap=1):
    """先黑后白: 与其它白色块之间留出黑缝。"""
    d.ellipse(box, fill=BLACK)
    x0, y0, x1, y1 = box
    d.ellipse([x0 + gap, y0 + gap, x1 - gap, y1 - gap], fill=WHITE)


def ring_ellipse(d, box, gap=1):
    d.ellipse(box, fill=BLACK)
    x0, y0, x1, y1 = box
    d.ellipse([x0 + gap, y0 + gap, x1 - gap, y1 - gap], fill=WHITE)


def draw_sprite(blink):
    img = Image.new("1", (SP, SP), BLACK)
    d = ImageDraw.Draw(img)

    # ---- 1. 头发与双马尾: 先画白, 再用 50% 棋盘格"压暗"成灰调 ----
    d.ellipse([7, 5, 57, 55], fill=WHITE)
    d.ellipse([1, 24, 13, 56], fill=WHITE)
    d.ellipse([51, 24, 63, 56], fill=WHITE)

    px = img.load()
    for y in range(SP):
        for x in range(SP):
            if px[x, y] and ((x + y) & 1) == 0:
                px[x, y] = BLACK

    # ---- 2. 发带(白) + 蕾丝(黑弧) ----
    d.arc([9, 1, 55, 33], 185, 355, fill=WHITE)
    d.arc([12, 4, 52, 30], 185, 355, fill=WHITE)
    x = 14
    while x < 52:
        d.arc([x - 2, 7, x + 2, 12], 0, 180, fill=BLACK)
        x += 4

    # ---- 3. 脸(实心白, 与头发留黑缝) ----
    ring_ellipse(d, [17, 17, 47, 47], 1)
    # 刘海尖角
    for cx in (23, 32, 41):
        d.polygon([(cx - 5, 18), (cx + 5, 18), (cx, 26)], fill=BLACK)
    # 鬓角
    d.polygon([(17, 24), (20, 24), (19, 40)], fill=BLACK)
    d.polygon([(47, 24), (44, 24), (45, 40)], fill=BLACK)

    # ---- 4. 眼睛 ----
    if blink:
        d.line([23, 32, 30, 32], fill=BLACK)
        d.line([34, 32, 41, 32], fill=BLACK)
    else:
        d.ellipse([23, 27, 30, 37], fill=BLACK)
        d.ellipse([34, 27, 41, 37], fill=BLACK)
        d.point((25, 29), fill=WHITE); d.point((26, 29), fill=WHITE)
        d.point((36, 29), fill=WHITE); d.point((37, 29), fill=WHITE)
        d.point((25, 34), fill=WHITE)
        d.point((36, 34), fill=WHITE)
    d.arc([30, 37, 34, 41], 20, 160, fill=BLACK)

    # ---- 5. 衣领 + 蝴蝶结 ----
    ring_ellipse(d, [19, 46, 45, 63], 1)
    d.polygon([(32, 54), (24, 49), (24, 59)], fill=BLACK)
    d.polygon([(32, 54), (40, 49), (40, 59)], fill=BLACK)
    d.rectangle([30, 52, 34, 56], fill=BLACK)

    return img


def compose(blink, dy):
    canvas = Image.new("1", (W, H), BLACK)
    canvas.paste(draw_sprite(blink), (OX, OY + dy))
    d = ImageDraw.Draw(canvas)
    # 底部水线
    d.line([0, 62, W - 1, 62], fill=WHITE)
    # 两颗气泡
    d.ellipse([104, 18 + (dy * 2), 108, 22 + (dy * 2)], outline=WHITE)
    d.ellipse([112, 34 - (dy * 2), 115, 37 - (dy * 2)], outline=WHITE)
    return canvas


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
    frames = [compose(False, 0), compose(False, -1), compose(False, 0), compose(True, -1)]
    here = os.path.dirname(__file__)
    out_png = os.path.join(here, "..", "out", "whale2_preview.png")
    out_h = os.path.join(here, "..", "src", "anim_whale.h")

    sheet = Image.new("RGB", (4 * (W * 4 + 8) + 8, H * 4 + 16), (20, 20, 20))
    for i, f in enumerate(frames):
        sheet.paste(f.convert("RGB").resize((W * 4, H * 4), Image.NEAREST), (8 + i * (W * 4 + 8), 8))
    sheet.save(out_png)

    data = [to_bytes(f) for f in frames]
    with open(out_h, "w", encoding="utf-8") as fh:
        fh.write("/* 自动生成: 鲸鱼娘像素风 %d 帧 128x64, 勿手改 */\n" % len(frames))
        fh.write("#ifndef ANIM_WHALE_H\n#define ANIM_WHALE_H\n\n")
        fh.write("#define ANIM_FRAMES %d\n" % len(frames))
        fh.write("static const unsigned char anim_whale[%d][%d] = {\n" % (len(frames), W * 8))
        for n, d in enumerate(data):
            fh.write("  { /* frame %d */\n" % n)
            for p in range(8):
                fh.write("    " + ",".join("0x%02X" % c for c in d[p * W:(p + 1) * W]) + ",\n")
            fh.write("  },\n")
        fh.write("};\n\n#endif\n")
    print("frames", len(frames), "->", out_h, "preview", out_png)


if __name__ == "__main__":
    main()
