"""The DualSense of the native KEY CONFIGURATION page (art/pad_dualsense.png), in the PS1 pad
picture's place: 144 x 136 frame-buffer units, whose units are 5/3 as wide as tall (384 x 480 in
the 4:3 frame). Drawn in square units on a 240 x 136 canvas, 8x supersampled, then narrowed to
576 x 544 (4x the picture, sharp on a phone).

    python art/draw_dualsense.py art/pad_dualsense.png
"""
import sys
from PIL import Image, ImageDraw, ImageFilter, ImageChops

NW, NH = 240, 136          # the canvas in square units (144 * 5 / 3 wide)
OUT = (576, 544)
S = 8
W, H = NW * S, NH * S
MID = NW / 2


def P(x, y):
    return (x * S, y * S)


def B(x0, y0, x1, y1):
    return (x0 * S, y0 * S, x1 * S, y1 * S)


def C(cx, cy, r):
    """The box of a circle."""
    return B(cx - r, cy - r, cx + r, cy + r)


def mx(x):
    return NW - x


def new_mask():
    m = Image.new("L", (W, H), 0)
    return m, ImageDraw.Draw(m)


def grip(d, top, bottom, width):
    d.line([P(*top), P(*bottom)], fill=255, width=int(width * S))
    d.ellipse(C(*bottom, width / 2), fill=255)
    d.ellipse(C(*top, width / 2), fill=255)


def gradient(top, bottom, y0, y1):
    g = Image.new("RGB", (1, H))
    for y in range(H):
        t = min(1.0, max(0.0, (y / S - y0) / (y1 - y0)))
        g.putpixel((0, y), tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(3)))
    return g.resize((W, H))


# The body: a wide top and two grips sweeping down and out.
body, bd = new_mask()
bd.rounded_rectangle(B(28, 30, mx(28), 86), 20 * S, fill=255)
for side in (lambda x: x, mx):
    grip(bd, (side(56), 66), (side(40), 116), 34)
bd.polygon([P(52, 72), P(mx(52), 72), P(mx(78), 106), P(78, 106)], fill=255)

# The black core under the touchpad: around the d-pad, the buttons and the sticks.
DX, FX, BY = 58, mx(58), 62          # d-pad and face buttons' centres
core, cd = new_mask()
cd.rounded_rectangle(B(50, 44, mx(50), 104), 20 * S, fill=255)
cd.ellipse(C(DX, BY, 21), fill=255)
cd.ellipse(C(FX, BY, 21), fill=255)
core = ImageChops.multiply(core, body)

img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
d = ImageDraw.Draw(img)
# Shoulders (L1/R1, the triggers behind), peeking over the body.
for x0, x1 in ((34, 88), (mx(88), mx(34))):
    d.rounded_rectangle(B(x0, 21, x1, 40), 7 * S, fill=(30, 31, 36, 255))
    d.rounded_rectangle(B(x0 + 3, 22, x1 - 3, 27), 3 * S, fill=(78, 80, 90, 255))
# A soft shadow.
shadow = body.filter(ImageFilter.GaussianBlur(3 * S)).point(lambda v: int(v * 0.5))
img.paste((0, 0, 0, 255), (int(1.5 * S), int(3 * S)), shadow)
# The white shell, darker towards the grips' ends, with a shaded rim.
img.paste(gradient((252, 252, 253), (172, 176, 188), 30, 132), (0, 0), body)
rim = ImageChops.subtract(body, body.filter(ImageFilter.GaussianBlur(2 * S)).point(lambda v: 255 if v > 250 else v))
img.paste((140, 144, 156), (0, 0), rim.point(lambda v: min(255, v * 2)))
# The black core with a glossy top.
img.paste(gradient((56, 58, 68), (18, 19, 23), 44, 104), (0, 0), core)

# The touchpad and the light bar along its sides.
d.rounded_rectangle(B(84, 27, mx(84), 60), 8 * S, fill=(214, 216, 222))
d.rounded_rectangle(B(85, 28, mx(85), 58), 7 * S, fill=(246, 246, 248))
for x0, x1 in ((84.5, 86.5), (mx(84.5), mx(86.5))):
    d.line([P(x0, 36), P(x1, 58)], fill=(70, 150, 255), width=int(1.3 * S))
# Create and Options.
d.rounded_rectangle(B(77, 30, 80.5, 38), 1.7 * S, fill=(44, 46, 52))
d.rounded_rectangle(B(mx(80.5), 30, mx(77), 38), 1.7 * S, fill=(44, 46, 52))

# The d-pad: four arrows pointing in, apart.
grey, edge = (160, 164, 174), (92, 95, 104)
for ax, ay in ((0, -1), (0, 1), (-1, 0), (1, 0)):
    def p(a, b):   # a: along the arm, b: across
        return P(DX + ax * a + ay * b, BY + ay * a + ax * b)
    d.polygon([p(3, 0), p(6.5, -3.8), p(15, -3.8), p(15, 3.8), p(6.5, 3.8)], fill=grey, outline=edge,
              width=int(0.6 * S))

# Face buttons; the symbols in the game's colours (LP square, RP triangle, LK cross, RK circle).
step, r = 10.5, 5.6
buttons = {
    "triangle": ((FX, BY - step), (70, 205, 160)),
    "circle": ((FX + step, BY), (240, 90, 80)),
    "square": ((FX - step, BY), (235, 120, 215)),
    "cross": ((FX, BY + step), (110, 150, 245)),
}
for name, ((bx, by), col) in buttons.items():
    d.ellipse(C(bx, by, r), fill=(14, 14, 18), outline=(96, 98, 108), width=int(0.7 * S))
    k, w = 2.5, int(0.9 * S)
    if name == "triangle":
        d.polygon([P(bx, by - k - 0.3), P(bx + k, by + k - 0.8), P(bx - k, by + k - 0.8)], outline=col, width=w)
    elif name == "circle":
        d.ellipse(C(bx, by, k), outline=col, width=w)
    elif name == "square":
        d.rectangle(B(bx - k + 0.3, by - k + 0.3, bx + k - 0.3, by + k - 0.3), outline=col, width=w)
    else:
        d.line([P(bx - k, by - k), P(bx + k, by + k)], fill=col, width=w)
        d.line([P(bx - k, by + k), P(bx + k, by - k)], fill=col, width=w)

# The sticks.
for sx in (90, mx(90)):
    sy = 84
    d.ellipse(C(sx, sy, 11.5), fill=(8, 8, 10))
    d.ellipse(C(sx, sy, 9.5), fill=(52, 54, 62))
    d.ellipse(C(sx, sy, 7), fill=(30, 31, 36))
    d.ellipse(B(sx - 7, sy - 8, sx + 5.5, sy + 1.5), fill=(70, 72, 82))
# The PS button and the mute button.
d.ellipse(C(MID, 87, 3.6), fill=(16, 16, 18), outline=(110, 112, 120), width=int(0.5 * S))
d.rounded_rectangle(B(MID - 3.6, 97, MID + 3.6, 99.5), 1.2 * S, fill=(255, 150, 130))

img.resize(OUT, Image.LANCZOS).save(sys.argv[1])
