### Translating `remake/presentation` (on top of RULES.md)

Everything in RULES.md holds (names identical, statement for statement, `gd::Int` for game ints,
precedence, evaluation order, loop ranges evaluated once). Presentation code may use `float`
where the GDScript uses float (window coordinates, colours, alpha); game values stay `gd::Int`.

#### Finished examples (read them first)

`src/presentation/screens/screen_canvas.{h,cpp}`, `psx_canvas.{h,cpp}`, `main_menu_view.{h,cpp}`,
`screen_mask.h`, `fade_overlay.h`, `src/presentation/hud/hud_text.{h,cpp}`, `src/gfx/canvas.h`,
`src/content/vram_image.h`, `src/content/hud_data.h`, `src/content/texture_packs.h`,
`src/app/game_app.cpp` (how views are set up, fed with `show_state` and drawn).

#### Godot to C++

| Godot | C++ |
|---|---|
| `extends ScreenCanvas` / `PsxCanvas` | `class X : public ScreenCanvas` / `PsxCanvas`; `_draw()` is `void _draw() override` |
| `extends Control` with `_draw` (no ScreenCanvas) | a class with `bool visible; gfx::Vec2 size;` and `void draw(gfx::Canvas&, gfx::Vec2 window)` that sets `size` and draws |
| `Texture2D`, `ImageTexture` | `gfx::TextureRef` (null-able); `tex.get_size()` is `(tex->width(), tex->height())` |
| `ImageTexture.create_from_image(img)` | `std::make_shared<gfx::Texture>(image)` with a `gfx::Image` |
| `TexturePacks.texture(path)` | `TexturePacks::texture(path)` |
| `Color`, `Color8(r,g,b,a)`, `Color.BLACK` | `gfx::Color`, `gfx::Color::c8(r,g,b,a)`, `gfx::Color::black()`; other named colours as literals |
| `Vector2`, `Rect2`, `Rect2i`, `Vector2i` (screen geometry) | `gfx::Vec2`, `gfx::Rect2` (floats) |
| `draw_rect`, `draw_texture_rect`, `draw_texture_rect_region`, `draw_polygon`, `draw_primitive`, `draw_line`, `draw_circle`, `draw_arc`, `draw_set_transform` | the same names on `target->` (ScreenCanvas) or the `gfx::Canvas&` |
| `add_pass(CanvasItemMaterial.BLEND_MODE_ADD, _f, clip)` | `add_pass(gfx::Blend::ADD, [this] { _f(); }, clip)` |
| `queue_redraw()`, `redraw()` | keep the call (no-op); views are drawn every frame |
| `visible = ...` | the `visible` member; the app draws only visible views |
| `Font`, `ThemeDB.fallback_font`, `FontVariation` (`variation_embolden`) | `gfx::FontRef` (`src/gfx/font.h`): `gfx::Font::fallback()`, `font->with_embolden(x)` |
| `draw_string(font, pos, text, align, width, size, modulate)`, `draw_string_outline(...)`, `draw_multiline_string(...)`, `font.get_string_size(...)`, `get_ascent`, `get_height` | the same methods on the font, with the canvas first: `font->draw_string(canvas, pos, text, gfx::HorizontalAlignment::LEFT, width, size, modulate)`; `HORIZONTAL_ALIGNMENT_*` is `gfx::HorizontalAlignment::*` |
| `Label` | report it; draw its text with the font where the remake lays it out |
| `Tween`, `AnimationPlayer`, `Timer` | report them; translate their effect as explicit per-frame state if it is simple |
| signals / `Callable` | `std::function`, report them |

Paths: views receive the screens' directory or `HudData` in `setup` like the GDScript; never
hard-code paths.

#### Godot 3D to C++ (`src/gfx3d`)

`src/gfx3d/{math3d,resources,nodes}.h` mirror Godot's 3D API so 3D views translate line by line;
read those three headers first. The renderer (`renderer.h`) draws a node tree; views never call GL.

| Godot | C++ |
|---|---|
| `Vector3`, `Vector4`, `Basis`, `Transform3D`, `Projection`, `AABB` | the same names in `t3` (float). `basis.x/.y/.z` are `basis.x()/y()/z()` (columns); `Basis(x, y, z)` takes columns; `Vector3.UP` is `Vector3::UP()` |
| `extends Node3D` | `class X : public Node3D`; children with `add_child(gd::Ref<Node3D>)`; nodes are made with `gd::make<MeshInstance3D>()` and held as `gd::Ref<...>` |
| `position`, `rotation`, `scale`, `global_basis`, `global_position`, `transform` | `transform.origin` / `set_position`, `set_rotation`, `set_scale`, `get_global_basis()`, `get_global_position()`, `transform` |
| `MeshInstance3D`, `MultiMeshInstance3D`, `Camera3D`, `DirectionalLight3D`, `OmniLight3D`, `Environment` | same names, same members (`layers`, `cast_shadow`, `material_override`, `custom_aabb`, `light_cull_mask`, `omni_range` ...) |
| `ArrayMesh.add_surface_from_arrays(PRIMITIVE_TRIANGLES, arrays, [], {}, format)` | `mesh->add_surface_from_arrays(Primitive::TRIANGLES, MeshArrays{...})`: `MeshArrays` has `vertex`, `normal`, `tex_uv`, `tex_uv2`, `color`, `custom[0..3]` (floats) with `custom_components[i]`, `index`. Half formats (`encode_half`) are given as plain floats |
| `ImmediateMesh` (`surface_begin/set_uv/set_color/add_vertex/end`) | same methods |
| `MultiMesh` (`instance_count`, `set_instance_transform/color/custom_data`, `visible_instance_count`) | same; `instance_count = n` is `set_instance_count(n)` |
| `preload("res://...gdshader")` | `Shader::load("res://...gdshader")` (the file must exist in `t3native/shaders/`, copied verbatim from the remake) |
| `ShaderMaterial`, `set_shader_parameter(k, v)` | same; values are `ShaderValue` (bool, int, float, `gfx::Vec2`, `Vector3`, `Vector4`, `gfx::Color`, `Basis`, `std::vector<Vector4>` for PackedVector4Array, `std::vector<float>`, `gfx::TextureRef`) |
| `StandardMaterial3D` (`shading_mode`, `blend_mode`, `transparency`, `albedo_color`, `albedo_texture`, `cull_mode`, `billboard_mode`, `no_depth_test`, `texture_filter`, `uv1_scale/offset`, ...) | same members and enum names |
| `RenderQuality.shader(base)` (CRISP variant) | `base->with_define("CRISP")` when the setting asks for it |
| `SubViewport` used as a 2D render target | report it; the app renders the 2D content into a texture |
| a view's `_process`/`_physics_process` | an explicit method the app calls, named after what it does |

#### Tests

If `remake/tests/presentation/test_<view>.gd` exists for your view, port it to
`tests/presentation/test_<view>.cpp` (see `tests/platform/test_input_router.cpp` for the style,
`tests/support/test_suite.h` for `T3_TEST`, `t.expect`, `t.expect_equal`, `t.require`,
`t3test::imported(...)`). Tests that need a Godot scene tree or a renderer: skip and report.
