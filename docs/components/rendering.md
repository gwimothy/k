# Rendering

**Code:** `engine/render/`, shaders in `base/shaders/`

OpenGL 4.5 core. Each frame:

```
scene → HDR target (RGBA16F, MSAA) → resolve → postprocess (exposure, ACES, gamma) → window → ImGui (HUD, menu, console)
```

Inside the scene pass, in order: world (lightmapped, PVS + frustum culled) and brush entities
→ props (models) → sky (at the far plane) → decals and particles → translucent surfaces → debug lines.

## Materials

A material is a KeyValues file at `base/materials/<name>.kmt`, the same idea as Source's `.vmt`:

```
"LitGeneric"
{
    "$basetexture"  "dev/dev_orange"   // materials/dev/dev_orange.png
    "$color"        "[1 1 1]"
    "$surfaceprop"  "metal"
}
```

| Key | Meaning |
|---|---|
| shader name (first line) | `LitGeneric` (lit) or `UnlitGeneric` |
| `$basetexture` | Texture under `materials/` without extension (png, tga, jpg) |
| `$color` | Tint `[r g b]` |
| `$alpha` | Opacity; < 1 makes it translucent |
| `$translucent` | Alpha blend using the texture's alpha |
| `$alphatest` / `$alphatestreference` | Cut-out transparency (default threshold 0.5) |
| `$nocull` | Draw both sides |
| `$selfillum` | Ignore lighting |
| `$texscale` | Repeat the texture N times more often |
| `$surfaceprop` | Surface type (for footsteps/impacts later) |

If there's no `.kmt` but a texture with the same name exists, a `LitGeneric` material is
created for it automatically. Anything missing renders as Source's magenta/black checker and
logs a warning (`mat_showmissing`). `mat_reloadall` reloads textures and materials.

On world surfaces `LitGeneric` uses the `lightmapped_generic` shader (texture × baked
lightmap). On models it uses `lit_generic` (sun + sky ambient from the map's
`light_environment`).

## Textures

`render/texture.*`: loaded with stb_image, uploaded as sRGB with mipmaps and anisotropic
filtering (`mat_anisotropy`). Sky cubemaps are six images
`materials/skybox/<name>_{px,nx,py,ny,pz,nz}.png` (`r_skyname`, or worldspawn `skyname`).

## Models

glTF 2.0 binary (`.glb`) via cgltf (`render/model.*`, `render/gltf_loader.cpp`). Each glTF
material name is looked up as an engine material, so a glTF material named
`models/props/crate` uses `materials/models/props/crate.kmt`. Models are expected in engine
units, Z up, origin at the base.

## Shaders

GLSL 450 in `base/shaders/<name>.vert` + `.frag`, loaded through the `ShaderLibrary`. With
`r_shader_hotreload 1` (default), editing a shader file recompiles it within half a second; a
broken edit logs the error and keeps the last working version. `r_reloadshaders` forces it.

## World rendering

`render/world_renderer.*`. Every frame:
1. Find the camera's leaf and decompress its PVS (the leaves visible from it).
2. Skip leaves not in the PVS or outside the view frustum.
3. Collect their faces, batch them by material, and draw each batch with one `glMultiDrawElements`.

Brush entities (doors, lifts) draw separately with a translation. Debug: `r_novis 1`
ignores the PVS, `r_lockpvs 1` freezes it so you can fly around and see what's culled,
`mat_fullbright 1` removes lighting, `mat_wireframe 1`.

## Effects

`render/effects.*`: client-only particles (camera-facing quads; dust, sparks, blood) and
impact decals (soft dark discs drawn procedurally in `particle.frag`, polygon-offset onto the
surface). The server triggers them with temp entities. `r_decals` caps decals (oldest are
replaced), `r_particles 0` hides particles.

## Debug drawing

`render/debug_draw.h`, callable from anywhere in the engine:

```cpp
debugdraw::line(a, b, {1, 0, 0}, /*seconds*/ 2.0f);
debugdraw::box(mins, maxs, {0, 1, 0});
debugdraw::cross(point, 16, {0, 1, 1});
```

Console: `debug_drawline`, `debug_drawcross`, `debug_clear`, `r_showbounds 1`, `r_drawaxis 1`.

## Coordinates

Z up, X forward, Y left, 1 unit ≈ 1 inch. Angles are `QAngle{pitch, yaw, roll}` in degrees;
positive pitch looks down. FOV (`fov_desired`) is horizontal at 4:3 and widens for wider
screens, like Source.

## Screenshots

`screenshot` (F5) writes `base/screenshots/kNNNN.png` at the end of the current frame. In
scripts follow it with `+wait`, or later commands in the same frame show up in the shot.
