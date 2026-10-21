// remake/presentation/fighter/curved_mesh.gd
#include "presentation/fighter/curved_mesh.h"

#include <cmath>
#include <memory>
#include <vector>

namespace t3 {

namespace {

// Godot's Mesh.ArrayCustomFormat and ArrayFormat shift (format()).
constexpr gd::Int MESH_ARRAY_CUSTOM_RGBA_FLOAT = 7;
constexpr gd::Int MESH_ARRAY_FORMAT_CUSTOM0_SHIFT = 13;

}  // namespace

// The curved grids of `model`, built on first use and kept on the model.
gd::Ref<CurvedMesh> CurvedMesh::of(const gd::Ref<CharacterModel>& model) {
    if (model->curved_cache == nullptr) {
        model->curved_cache = gd::make<CurvedMesh>(model);
    }
    return std::static_pointer_cast<CurvedMesh>(model->curved_cache);
}

CurvedMesh::CurvedMesh(const gd::Ref<CharacterModel>& model) {
    _build_grid();
    for (const auto& surface : model->surfaces) {
        _arrays[surface] = _add(surface->vertices, model->vertex_floats);
    }
    for (const auto& group : model->variant_groups()) {
        for (const auto& variant : group->variants) {
            for (const auto& surface : variant) {
                _arrays[surface] = _add(surface->vertices, model->vertex_floats);
            }
        }
    }
    texture = _texture();
    _data = gd::Packed<float>();
}

// The mesh arrays of one of the model's surfaces.
MeshArrays CurvedMesh::arrays(const gd::Ref<CharacterModel::Surface>& surface) const { return _arrays[surface]; }

gd::Int CurvedMesh::format() { return MESH_ARRAY_CUSTOM_RGBA_FLOAT << MESH_ARRAY_FORMAT_CUSTOM0_SHIFT; }

void CurvedMesh::_build_grid() {
    for (gd::Int i = 0; i < LEVEL + 1; ++i) {
        for (gd::Int j = 0; j < LEVEL + 1 - i; ++j) {
            _grid_uvw.append(Vector3(static_cast<float>(LEVEL - i - j), static_cast<float>(i), static_cast<float>(j)) /
                             static_cast<float>(LEVEL));
        }
    }
    // Vertex (i, j) moves i steps towards corner 1 and j towards corner 2; both kinds of grid
    // triangle keep the winding of (corner 0, corner 1, corner 2).
    for (gd::Int i = 0; i < LEVEL; ++i) {
        for (gd::Int j = 0; j < LEVEL - i; ++j) {
            _grid_indices.append_array(gd::PI32{grid_index(i, j), grid_index(i + 1, j), grid_index(i, j + 1)});
            if (i + j + 1 < LEVEL) {
                _grid_indices.append_array(gd::PI32{grid_index(i + 1, j), grid_index(i + 1, j + 1), grid_index(i, j + 1)});
            }
        }
    }
}

// The index of grid vertex (i, j) in rows of LEVEL + 1, LEVEL, ... vertices.
gd::Int CurvedMesh::grid_index(gd::Int i, gd::Int j) { return i * (LEVEL + 1) - ((i * (i - 1)) >> 1) + j; }

// The grid arrays of a surface (converter corners of `stride` floats); stores its corners.
MeshArrays CurvedMesh::_add(const gd::Packed<float>& v, gd::Int stride) {
    const std::vector<float>& f = v.vec();
    gd::Int triangles = gd::div(gd::div(v.size(), stride), 3);
    MeshArrays arrays;   // positions (vertex), uvs (tex_uv), custom0, indices (index)
    std::vector<float>& custom0 = arrays.custom[0];
    std::vector<float>& data = _data.vec();
    for (gd::Int t = 0; t < triangles; ++t) {
        gd::Arr<gfx::Vec2> corner_uvs;
        for (gd::Int k = 0; k < 3; ++k) {
            size_t o = static_cast<size_t>((3 * t + k) * stride);
            corner_uvs.append(gfx::Vec2(f[o + 10], f[o + 11]));
            // pa, bone_a | pb, bone_b | s_own, wb + 2 · curves | s_other, bone_other | the game's n,
            // bone_n | pc, bone_c + wc
            data.insert(data.end(), {f[o], f[o + 1], f[o + 2], f[o + 12],
                f[o + 3], f[o + 4], f[o + 5], f[o + 13],
                f[o + 15], f[o + 16], f[o + 17], static_cast<float>(static_cast<double>(f[o + 6]) + 2.0 * f[o + 22]),
                f[o + 18], f[o + 19], f[o + 20], f[o + 21],
                f[o + 7], f[o + 8], f[o + 9], f[o + 14],
                f[o + 23], f[o + 24], f[o + 25], static_cast<float>(static_cast<double>(f[o + 27]) + f[o + 26])});
        }
        gd::Int base = static_cast<gd::Int>(arrays.vertex.size());
        for (const Vector3& w : _grid_uvw) {
            arrays.vertex.push_back(w);
            arrays.tex_uv.push_back(corner_uvs[0] * w.x + corner_uvs[1] * w.y + corner_uvs[2] * w.z);
            custom0.insert(custom0.end(), {static_cast<float>(_triangles + t), w.x, w.y, w.z});
        }
        for (gd::Int index : _grid_indices) {
            arrays.index.push_back(static_cast<int>(base + index));
        }
    }
    _triangles += triangles;
    return arrays;
}

gfx::TextureRef CurvedMesh::_texture() {
    gd::Int texels = _data.size() / 4;
    gd::Int height = gd::maxi(1, static_cast<gd::Int>(std::ceil(static_cast<double>(texels) / DATA_WIDTH)));
    gd::Packed<float> data = _data.duplicate();
    data.resize(DATA_WIDTH * height * 4);
    // TODO: gfx3d: Image.create_from_data(DATA_WIDTH, height, false, Image.FORMAT_RGBAF,
    // data.to_byte_array()) and ImageTexture.create_from_image: gfx::Image and gfx::Texture hold
    // RGBA8 only, so there is no float texture to make; the "curved" shading has no corner data.
    return nullptr;
}

}  // namespace t3
