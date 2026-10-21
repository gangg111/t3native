#pragma once
// remake/presentation/fighter/curved_mesh.gd
// The fighters' curved surfaces (the Shading setting's "curved"): every triangle of a model is
// drawn as a grid of LEVEL x LEVEL smaller ones whose vertices the shader lifts onto the
// triangle's PN patch (curved point-normal triangles) after skinning its three corners.
//
// The corners of every surface the model has (body, hand and wing variants) go into one float texture,
// CORNER_TEXELS texels each (fighter_skin.gdshaderinc, corner()); a grid vertex keeps its
// triangle's index and its barycentric weights in CUSTOM0 and the interpolated UV. Built once
// per model (`of`) and shared by every view of it.
#include "content/character_model.h"
#include "gd/gd.h"
#include "gfx/texture.h"
#include "gfx3d/math3d.h"
#include "gfx3d/resources.h"

namespace t3 {

class CurvedMesh {
public:
    static constexpr gd::Int LEVEL = 3;
    static constexpr gd::Int DATA_WIDTH = 1024;   // texels per row of the corner texture
    static constexpr gd::Int CORNER_TEXELS = 6;

    gfx::TextureRef texture;                      // the corners of every surface (TODO: gfx3d float textures: stays null)
    gd::Dict<gd::Ref<CharacterModel::Surface>, MeshArrays> _arrays;   // CharacterModel.Surface -> its grid's mesh arrays
    gd::Packed<float> _data;                      // RGBA per texel (floats: written through vec())
    gd::Int _triangles = 0;
    gd::Arr<Vector3> _grid_uvw;                   // barycentric weights of the grid's vertices
    gd::PI32 _grid_indices;

    // The curved grids of `model`, built on first use and kept on the model.
    static gd::Ref<CurvedMesh> of(const gd::Ref<CharacterModel>& model);

    explicit CurvedMesh(const gd::Ref<CharacterModel>& model);

    // The mesh arrays of one of the model's surfaces.
    MeshArrays arrays(const gd::Ref<CharacterModel::Surface>& surface) const;
    // Godot's array format bits (CUSTOM0 RGBA_FLOAT); MeshArrays carries the components itself.
    static gd::Int format();
    void _build_grid();
    // The index of grid vertex (i, j) in rows of LEVEL + 1, LEVEL, ... vertices.
    static gd::Int grid_index(gd::Int i, gd::Int j);
    // The grid arrays of a surface (converter corners of `stride` floats); stores its corners.
    MeshArrays _add(const gd::Packed<float>& v, gd::Int stride);
    gfx::TextureRef _texture();
};

}  // namespace t3
