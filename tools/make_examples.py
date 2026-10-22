"""Assembles the example images rendered by tools/examples.cpp.

    python3 tools/make_examples.py <ppm-dir> <out-dir>

Writes before_after.png, modes.png and animation.gif (needs Pillow).
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

FONTS = [
    "/usr/share/fonts/opentype/inter/Inter-SemiBold.otf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
    "/System/Library/Fonts/Helvetica.ttc",
    "C:/Windows/Fonts/segoeuib.ttf",
]


def font(size):
    for path in FONTS:
        if os.path.exists(path):
            return ImageFont.truetype(path, size)
    return ImageFont.load_default()


def label(im, text, size=18):
    draw = ImageDraw.Draw(im, "RGBA")
    f = font(size)
    x0, y0, x1, y1 = draw.textbbox((0, 0), text, font=f)
    pad = size // 2
    box = (pad, pad, pad + (x1 - x0) + 2 * pad, pad + (y1 - y0) + 2 * pad)
    draw.rounded_rectangle(box, radius=pad, fill=(0, 0, 0, 160))
    draw.text((box[0] + pad - x0, box[1] + pad - y0), text, font=f, fill=(255, 255, 255, 255))
    return im


def grid(images, cols, gap, background=(24, 24, 28)):
    w, h = images[0].size
    rows = (len(images) + cols - 1) // cols
    out = Image.new("RGB", (cols * w + (cols - 1) * gap, rows * h + (rows - 1) * gap), background)
    for i, im in enumerate(images):
        out.paste(im, ((i % cols) * (w + gap), (i // cols) * (h + gap)))
    return out


def main():
    src, dst = sys.argv[1], sys.argv[2]
    os.makedirs(dst, exist_ok=True)
    load = lambda name: Image.open(os.path.join(src, name + ".ppm")).convert("RGB")

    before_after = grid([label(load("original"), "Original"),
                         label(load("optical"), "Bright Times · Optical, Spectrum")], 2, 6)
    before_after.save(os.path.join(dst, "before_after.png"), optimize=True)

    panels = [
        ("optical", "Optical · Spectrum, 7 groups"),
        ("harris", "Optical · RGB Tri-Color, Cyclic (Harris shutter)"),
        ("trail", "Past only · Color Order: Past to Future"),
        ("palette", "Tinted Trails · Custom Palette (3 colours)"),
        ("glow", "Lighten · Spectrum"),
        ("original", "Original"),
    ]
    small = [label(load(name).resize((480, 270), Image.LANCZOS), text, 14) for name, text in panels]
    grid(small, 2, 6).save(os.path.join(dst, "modes.png"), optimize=True)

    frames = []
    i = 0
    while os.path.exists(os.path.join(src, "anim_orig_%02d.ppm" % i)):
        pair = grid([label(load("anim_orig_%02d" % i), "Original", 13),
                     label(load("anim_fx_%02d" % i), "Bright Times", 13)], 2, 4)
        frames.append(pair.quantize(colors=255, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE))
        i += 1
    if frames:
        frames[0].save(os.path.join(dst, "animation.gif"), save_all=True, append_images=frames[1:],
                       duration=40, loop=0, optimize=True)
    print("written to", dst)


if __name__ == "__main__":
    main()
