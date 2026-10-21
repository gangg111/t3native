"""A pad photo fitted to the KEY CONFIGURATION pad picture: cut out, cropped, scaled into 240 x 136
square units (the 144 x 136 picture's units are 5/3 as wide as tall), then narrowed to 576 x 544
(4x the picture). The photo stays private (art/private/).

The cut: the pad is neutral grey, its edge in the photo a fringe of bluish compression noise, so
the mask is the pixels near grey (|b - r| small) that the photo keeps; holes in it (the coloured
symbols, the logo) are filled by flooding the outside from the border. Then shrunk and softened.

    python art/fit_pad_photo.py photo.webp art/private/pad_dualsense_photo.png
"""
import sys
from PIL import Image, ImageChops, ImageDraw, ImageFilter

NW, NH = 240, 136
SCALE = 4
OUT = (144 * SCALE, NH * SCALE)
MARGIN = 2                 # units kept clear around the pad
TINT = 40                  # |b - r| above this is fringe, not the grey pad
EDGE = 5                   # the mask's shrink (a MinFilter size, source pixels)

src = Image.open(sys.argv[1]).convert("RGBA")
r, g, b, a = src.split()
grey = ImageChops.difference(b, r).point(lambda v: 255 if v <= TINT else 0)
mask = ImageChops.multiply(grey, a.point(lambda v: 255 if v > 128 else 0))
# The outside: flood from a border pixel over the not-pad; whatever it does not reach is pad.
outside = mask.point(lambda v: 0 if v else 128)
outside = ImageChops.add(outside, Image.new("L", src.size, 0))
framed = Image.new("L", (src.width + 2, src.height + 2), 128)
framed.paste(outside, (1, 1))
ImageDraw.floodfill(framed, (0, 0), 255)
mask = framed.crop((1, 1, src.width + 1, src.height + 1)).point(lambda v: 0 if v == 255 else 255)
mask = mask.filter(ImageFilter.MaxFilter(3)).filter(ImageFilter.MinFilter(3))   # close pinholes
mask = mask.filter(ImageFilter.MinFilter(EDGE)).filter(ImageFilter.GaussianBlur(1.2))
src.putalpha(mask)

src = src.crop(mask.point(lambda v: 255 if v > 8 else 0).getbbox())
room_w, room_h = (NW - 2 * MARGIN) * SCALE, (NH - 2 * MARGIN) * SCALE
k = min(room_w / src.width, room_h / src.height)
pad = src.resize((round(src.width * k), round(src.height * k)), Image.LANCZOS)
natural = Image.new("RGBA", (NW * SCALE, NH * SCALE), (0, 0, 0, 0))
natural.alpha_composite(pad, ((natural.width - pad.width) // 2, (natural.height - pad.height) // 2))
natural.resize(OUT, Image.LANCZOS).save(sys.argv[2])
