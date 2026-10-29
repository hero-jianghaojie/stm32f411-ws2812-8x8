"""把视频片段转成 128x64 单色帧序列, 输出 src/anim_video.h。

用法: python tools/gen_video.py   (先在 assets/frames 里用 ffmpeg 抽好帧)
处理: 亮度提升(gamma) -> Floyd-Steinberg 抖动 -> 居中放入 128x64
"""
import glob
import os
from PIL import Image, ImageOps, ImageFilter

W, H = 128, 64
GAMMA = 1.2
THRESH = 140
HERE = os.path.dirname(__file__)
FRAMES_GLOB = os.path.join(HERE, "..", "assets", "frames", "*.png")
OUT_H = os.path.join(HERE, "..", "src", "anim_video.h")
OUT_PNG = os.path.join(HERE, "..", "out", "video_frames.png")


def load(path):
    im = Image.open(path).convert("L")
    im = ImageOps.autocontrast(im, cutoff=1)
    im = im.point(lambda v: int(255 * ((v / 255) ** (1.0 / GAMMA))))
    im = im.filter(ImageFilter.MedianFilter(3))
    b = im.point(lambda v: 255 if v > THRESH else 0, "1")
    c = Image.new("1", (W, H), 0)
    c.paste(b, ((W - b.size[0]) // 2, (H - b.size[1]) // 2))
    return c


def to_bytes(img):
    px = img.load()
    out = bytearray(W * 8)
    for page in range(8):
        for col in range(W):
            v = 0
            for bit in range(8):
                if px[col, page * 8 + bit]:
                    v |= 1 << bit
            out[page * W + col] = v
    return bytes(out)


def main():
    files = sorted(glob.glob(FRAMES_GLOB))
    frames = [load(f) for f in files]
    n = len(frames)

    picks = [frames[0], frames[n // 4], frames[n // 2], frames[3 * n // 4]]
    sheet = Image.new("RGB", (4 * (W * 4 + 8) + 8, H * 4 + 16), (20, 20, 20))
    for i, f in enumerate(picks):
        sheet.paste(f.convert("RGB").resize((W * 4, H * 4), Image.NEAREST), (8 + i * (W * 4 + 8), 8))
    sheet.save(OUT_PNG)

    data = [to_bytes(f) for f in frames]
    with open(OUT_H, "w", encoding="utf-8") as fh:
        fh.write("/* 自动生成: 视频转 1-bit, %d 帧 128x64 */\n" % n)
        fh.write("#ifndef ANIM_VIDEO_H\n#define ANIM_VIDEO_H\n\n")
        fh.write("#define VID_FRAMES %d\n" % n)
        fh.write("static const unsigned char anim_video[%d][%d] = {\n" % (n, W * 8))
        for i, d in enumerate(data):
            fh.write("  { /* frame %d */\n" % i)
            for p in range(8):
                fh.write("    " + ",".join("0x%02X" % c for c in d[p * W:(p + 1) * W]) + ",\n")
            fh.write("  },\n")
        fh.write("};\n\n#endif\n")
    print("frames", n, "bytes", n * W * 8, "->", OUT_H)


if __name__ == "__main__":
    main()
