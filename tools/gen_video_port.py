"""竖屏版: 人物按竖幅裁切 -> 64x128 竖画布 -> 旋转 90 度写入 128x64 显存。
配合"把 OLED 物理竖过来放"的摆法；若显示上下颠倒, 把 ROT 改成 90。
"""
import glob
import os
from PIL import Image, ImageOps, ImageFilter

W, H = 128, 64          # 显存尺寸
PW, PH = 64, 128        # 竖屏可见尺寸
CROP_PORTRAIT = True
ROT = -90               # 逆时针 90 度(正数则为顺时针)
THRESH = 140
HERE = os.path.dirname(__file__)
SRC_GLOB = os.path.join(HERE, "..", "assets", "frames", "*.png")
OUT_H = os.path.join(HERE, "..", "src", "anim_video.h")
OUT_PNG = os.path.join(HERE, "..", "out", "video_frames.png")


def render(path):
    im = Image.open(path).convert("L")
    im = ImageOps.autocontrast(im, cutoff=1)
    # 竖画布 64x128
    im = im.resize((PW, PH), Image.LANCZOS)
    im = im.filter(ImageFilter.MedianFilter(3))
    b = im.point(lambda v: 255 if v > THRESH else 0, "1")
    # 旋转成 128x64
    return b.rotate(ROT, expand=True)


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
    files = sorted(glob.glob(SRC_GLOB))
    frames = [render(f) for f in files]
    n = len(frames)

    picks = [frames[0], frames[n // 4], frames[n // 2], frames[3 * n // 4]]
    sheet = Image.new("RGB", (4 * (W * 4 + 8) + 8, H * 4 + 16), (20, 20, 20))
    for i, f in enumerate(picks):
        sheet.paste(f.convert("RGB").resize((W * 4, H * 4), Image.NEAREST), (8 + i * (W * 4 + 8), 8))
    sheet.save(OUT_PNG)

    data = [to_bytes(f) for f in frames]
    with open(OUT_H, "w", encoding="utf-8") as fh:
        fh.write("/* 自动生成: 竖屏版视频帧, %d 帧 128x64(旋转 %d 度) */\n" % (n, ROT))
        fh.write("#ifndef ANIM_VIDEO_H\n#define ANIM_VIDEO_H\n\n")
        fh.write("#define VID_FRAMES %d\n" % n)
        fh.write("static const unsigned char anim_video[%d][%d] = {\n" % (n, W * 8))
        for i, d in enumerate(data):
            fh.write("  { /* frame %d */\n" % i)
            for p in range(8):
                fh.write("    " + ",".join("0x%02X" % c for c in d[p * W:(p + 1) * W]) + ",\n")
            fh.write("  },\n")
        fh.write("};\n\n#endif\n")
    print("frames", n, "rot", ROT, "->", OUT_H)


if __name__ == "__main__":
    main()
