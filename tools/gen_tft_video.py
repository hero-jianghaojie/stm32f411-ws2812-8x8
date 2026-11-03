"""把视频帧转成 128x128 RGB565 帧序列, 输出 src/anim_tft.h + 预览图。

用法: python tools/gen_tft_video.py [--frames N]
素材: assets/full/*.png (全分辨率帧, 由 ffmpeg 抽取)
"""
import argparse
import glob
import os

from PIL import Image

W = H = 128
HERE = os.path.dirname(__file__)
OUT_H = os.path.join(HERE, "..", "src", "anim_tft.h")
OUT_PNG = os.path.join(HERE, "..", "out", "tft_anim_preview.png")


def to_rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3)


def preprocess(im):
    """提亮暗调场景 + 轻微对比度增强(暗色壁纸在 1.44 寸小屏上过暗)。"""
    im = im.convert("RGB")
    px = im.load()
    w, h = im.size
    for y in range(h):
        for x in range(w):
            r, g, b = px[x, y]
            r = min(255, int(r * 1.55 + 18))
            g = min(255, int(g * 1.55 + 22))
            b = min(255, int(b * 1.35 + 20))
            px[x, y] = (r, g, b)
    return im


def center_crop_square(im):
    w, h = im.size
    s = min(w, h)
    return im.crop(((w - s) // 2, (h - s) // 2, (w + s) // 2, (h + s) // 2))


def to_bytes(im):
    im = im.resize((W, H), Image.LANCZOS)
    out = bytearray(W * H * 2)
    px = im.load()
    i = 0
    for y in range(H):
        for x in range(W):
            c = to_rgb565(*px[x, y])
            out[i] = (c >> 8) & 0xFF
            out[i + 1] = c & 0xFF
            i += 2
    return bytes(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--frames", type=int, default=14)
    args = ap.parse_args()

    files = sorted(glob.glob(os.path.join(HERE, "..", "assets", "full", "*.png")))
    if not files:
        raise SystemExit("no frames in assets/full")

    n = min(args.frames, len(files))
    picks = [files[i * len(files) // n] for i in range(n)]

    imgs = []
    for f in picks:
        im = Image.open(f)
        im = center_crop_square(im)
        im = preprocess(im)
        imgs.append(im)

    # 预览: 4x4 拼版
    cols, rows = 4, (n + 3) // 4
    scale_p = 2
    pw = W * scale_p
    sheet = Image.new("RGB", (cols * (pw + 6) + 6, rows * (pw + 6) + 6), (16, 16, 16))
    for i, im in enumerate(imgs):
        small = im.resize((pw, pw), Image.LANCZOS)
        sheet.paste(small, (6 + (i % cols) * (pw + 6), 6 + (i // cols) * (pw + 6)))
    sheet.save(OUT_PNG)

    data = [to_bytes(im) for im in imgs]
    with open(OUT_H, "w", encoding="utf-8") as fh:
        fh.write("/* 自动生成: %d 帧 128x128 RGB565 */\n" % n)
        fh.write("#ifndef ANIM_TFT_H\n#define ANIM_TFT_H\n\n")
        fh.write("#define TFT_FRAMES %d\n" % n)
        fh.write("#define TFT_FRAME_BYTES %d\n\n" % (W * H * 2))
        fh.write("static const unsigned char anim_tft[%d][%d] = {\n" % (n, W * H * 2))
        for i, d in enumerate(data):
            fh.write("  { /* frame %d */\n" % i)
            for row in range(0, len(d), 32):
                fh.write("    " + ",".join("0x%02X" % b for b in d[row:row + 32]) + ",\n")
            fh.write("  },\n")
        fh.write("};\n\n#endif\n")
    print("frames", n, "bytes", n * W * H * 2, "->", OUT_H)


if __name__ == "__main__":
    main()
