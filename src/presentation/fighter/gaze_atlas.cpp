// remake/presentation/fighter/gaze_atlas.gd
#include "presentation/fighter/gaze_atlas.h"

#include <cmath>
#include <cstring>
#include <memory>

#include "content/json_file.h"

namespace t3 {

namespace {

// Godot's Image operations the class uses, on RGBA8 gfx::Images (the runtime has none).

// Image.resize(w, h, INTERPOLATE_NEAREST): Godot's _scale_nearest (integer source offsets).
void image_resize_nearest(gfx::Image& image, gd::Int width, gd::Int height) {
    gfx::Image out;
    out.width = static_cast<int>(width);
    out.height = static_cast<int>(height);
    out.rgba.assign(static_cast<size_t>(width * height * 4), 0);
    for (gd::Int i = 0; i < height; ++i) {
        gd::Int src_yofs = gd::div(i * image.height, height);
        for (gd::Int j = 0; j < width; ++j) {
            gd::Int src_xofs = gd::div(j * image.width, width);
            std::memcpy(out.pixel(static_cast<int>(j), static_cast<int>(i)),
                        image.pixel(static_cast<int>(src_xofs), static_cast<int>(src_yofs)), 4);
        }
    }
    image = std::move(out);
}

// Image.blit_rect(src, Rect2i(src_pos, src_size), dst): clipped to both images.
void image_blit_rect(gfx::Image& dst, const gfx::Image& src, gd::Int sx, gd::Int sy, gd::Int sw, gd::Int sh, gd::Int dx,
                     gd::Int dy) {
    // The source rectangle within the source.
    if (sx < 0) { sw += sx; dx -= sx; sx = 0; }
    if (sy < 0) { sh += sy; dy -= sy; sy = 0; }
    sw = gd::mini(sw, src.width - sx);
    sh = gd::mini(sh, src.height - sy);
    // And its destination within the destination.
    if (dx < 0) { sw += dx; sx -= dx; dx = 0; }
    if (dy < 0) { sh += dy; sy -= dy; dy = 0; }
    sw = gd::mini(sw, dst.width - dx);
    sh = gd::mini(sh, dst.height - dy);
    if (sw <= 0 || sh <= 0) return;
    for (gd::Int y = 0; y < sh; ++y) {
        std::memcpy(dst.pixel(static_cast<int>(dx), static_cast<int>(dy + y)),
                    src.pixel(static_cast<int>(sx), static_cast<int>(sy + y)), static_cast<size_t>(sw * 4));
    }
}

// Image.get_region(Rect2i): a picture of the rectangle's size (outside the image: transparent).
gd::Ref<gfx::Image> image_get_region(const gfx::Image& image, gd::Int x, gd::Int y, gd::Int w, gd::Int h) {
    auto out = gd::make<gfx::Image>();
    out->width = static_cast<int>(w);
    out->height = static_cast<int>(h);
    out->rgba.assign(static_cast<size_t>(gd::maxi(0, w * h * 4)), 0);
    image_blit_rect(*out, image, x, y, w, h, 0, 0);
    return out;
}

// GDScript roundi: half away from zero.
gd::Int roundi(double v) { return static_cast<gd::Int>(std::llround(v)); }

}  // namespace

GazeAtlas::GazeAtlas(const gd::Json& data, const gfx::Image& open, const gfx::Image& swapped, const gfx::Image& sheet) {
    gd::PI32 size = JsonFile::ints(data["size"]);
    _size = gd::Vector2i(size[0], size[1]);
    _patches = data["patches"];
    _bases.append(_rgba(open));
    _bases.append(_rgba(swapped));
    // A texture pack may replace one atlas and not the other: the texture, the patches' scale and
    // the working picture all follow the atlas as loaded, the other is scaled to it.
    if (_bases[1]->width != _bases[0]->width || _bases[1]->height != _bases[0]->height) {
        image_resize_nearest(*_bases[1], _bases[0]->width, _bases[0]->height);
    }
    _sheet = _rgba(sheet);
    _scale = static_cast<double>(_bases[0]->width) / static_cast<double>(gd::Int(_size.x));
    _sheet_scale = static_cast<double>(_sheet->width) / static_cast<double>(SHEET_WIDTH);
}

// The texture for the head palette (`swapped`) and each eye's shift (NO_SHIFT: as loaded).
gfx::TextureRef GazeAtlas::show(bool swapped, const gd::PI32& shifts) {
    bool changed = false;
    if (_work == nullptr || swapped != _swapped) {
        _swapped = swapped;
        _work = gd::make<gfx::Image>();
        *_work = *_bases[swapped ? 1 : 0];   // copy_from
        _shifts = gd::PI32{NO_SHIFT, NO_SHIFT};
        changed = true;
    }
    for (gd::Int eye = 0; eye < 2; ++eye) {
        if (shifts[eye] != _shifts[eye]) {
            _paste(eye, shifts[eye]);
            _shifts[eye] = shifts[eye];
            changed = true;
        }
    }
    if (texture == nullptr || texture->stale()) {   // first, or after a GL context loss
        texture = std::make_shared<gfx::Texture>(*_work);
    } else if (changed) {
        texture->update(*_work);
    }
    return texture;
}

// The picture the texture shows (tests read it: a headless renderer keeps no copy of a texture).
gd::Ref<gfx::Image> GazeAtlas::image() const { return _work; }

// Eye `eye`'s rectangles with the pictures of `shift`, or of the atlas as it is for NO_SHIFT.
void GazeAtlas::_paste(gd::Int eye, gd::Int shift) {
    // (_patches.get(str(int(_swapped)), {}) as Dictionary).get(str(eye))
    const gd::Json& palette = _patches.get(gd::str(_swapped ? 1 : 0));
    if (!palette.is_object()) {
        return;
    }
    const gd::Json& patch = palette.get(gd::str(eye));
    if (patch.is_null()) {
        return;
    }
    const gd::Json& rects = patch["rects"];
    gd::Json from = shift == NO_SHIFT ? gd::Json() : patch["shifts"][gd::str(shift)];
    gd::Int count = rects.size();
    for (gd::Int i = 0; i < count; ++i) {
        gd::PI32 r = JsonFile::ints(rects[i]);
        gd::Int tx = roundi(r[0] * _scale), ty = roundi(r[1] * _scale);
        gd::Int tw = roundi(r[2] * _scale), th = roundi(r[3] * _scale);
        gd::Ref<gfx::Image> picture;
        if (shift == NO_SHIFT) {
            picture = image_get_region(*_bases[_swapped ? 1 : 0], tx, ty, tw, th);
        } else {
            gd::PI32 at = JsonFile::ints(from[i]);
            picture = image_get_region(*_sheet, roundi(at[0] * _sheet_scale), roundi(at[1] * _sheet_scale),
                                       roundi(r[2] * _sheet_scale), roundi(r[3] * _sheet_scale));
            if (picture->width != tw || picture->height != th) {
                image_resize_nearest(*picture, tw, th);
            }
        }
        image_blit_rect(*_work, *picture, 0, 0, picture->width, picture->height, tx, ty);
    }
}

// Godot decompresses and converts to RGBA8; a gfx::Image is RGBA8 already (a copy, as get_image()).
gd::Ref<gfx::Image> GazeAtlas::_rgba(const gfx::Image& image) { return gd::make<gfx::Image>(image); }

}  // namespace t3
