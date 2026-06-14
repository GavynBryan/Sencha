# Pillar 3b — Brush Texturing & Projection UVs

**Status**: Working plan (2026-06-14). Layers on the brush mesh (03-); feeds the cook (05-).
**Owns Stage S5.**

> Product requirement (2026-06-14): brushes have **texturing/UV tools**, and **UVs update
> automatically as brushes are resized**. That last clause is a *design constraint*, not a
> feature toggle: it forbids storing baked UV coordinates and mandates a **projection**
> model (the Hammer "texture lock" / world-aligned-texture approach).

---

## 1. The core decision: projection UVs, never baked UVs

If a face stored explicit `(u,v)` per vertex, resizing the brush (moving a face, extruding,
clipping) would stretch or corrupt the mapping — exactly the failure the requirement rules
out. Instead, each face stores **how to project** texture space onto it, and the actual
`(u,v)` are **computed** from the vertex world/face position at render and cook time.

```cpp
// editor/level/brush/FaceMaterial.h  (new)
struct UvProjection {
    // Texture axes in brush-local space. For "world/box-aligned" textures these are derived
    // from the face normal's dominant axis (Hammer's default); for "face-aligned" they lie
    // in the face plane. Stored explicitly so the designer can rotate/justify freely.
    Vec3d  AxisU = { 1, 0, 0 };     // direction of +U in brush-local space (normalized)
    Vec3d  AxisV = { 0, 0, 1 };     // direction of +V
    Vec2f  Scale = { 1, 1 };        // world-units per texture tile (texel density)
    Vec2f  Offset = { 0, 0 };       // texture shift, in UV
    float  Rotation = 0.0f;         // degrees, about the face normal (applied to U/V axes)
    bool   WorldAligned = true;     // true: project from world/box axes (texture lock on
                                    //       resize); false: lock to the face as it deforms
};

struct FaceMaterial {
    AssetRef     Material;          // asset:// path to a .smat (engine material, 04-§4)
    UvProjection Uv;
};

// UV for a vertex of a face is computed, never stored:
inline Vec2f ProjectUv(const UvProjection& p, Vec3d localPos) {
    // rotate (AxisU,AxisV) by p.Rotation, then:
    //   u = dot(localPos, U) / Scale.x + Offset.x
    //   v = dot(localPos, V) / Scale.y + Offset.y
}
```

`FaceMaterial` attaches to each `BrushFace` (03-§2.1 left a hook for it). It is part of the
authored brush data, serialized in the `brush_meshes` sidecar (03-§5).

### 1.1 Why this makes "UVs survive resize" automatic

- **Resize / move face**: vertices move; `ProjectUv` recomputes from new positions against
  the *unchanged* projection → texture stays pinned to world/face space (no stretch). This is
  texture lock.
- **Extrude**: new side faces inherit the source face's `FaceMaterial` (or a default);
  projection still computes valid UVs because it is positional, not index-based.
- **Clip**: the cap face gets a default/inherited projection; existing faces keep theirs.
- Nothing recomputes or "rebakes" — there is no stored UV to invalidate.

### 1.2 World-aligned vs. face-aligned

- **World-aligned (default, Hammer-like)**: U/V derive from world axes by the face's dominant
  normal axis. Adjacent coplanar brushes tile seamlessly; resizing never slides the texture.
  The right default for level geometry.
- **Face-aligned**: U/V lie in the face plane and follow it as it rotates. Useful for angled
  surfaces a designer wants the texture glued to. A per-face toggle.

---

## 2. Texturing tools (editor UX)

A **Face edit mode** (03- sub-element selection) plus a **Material/Texture panel**:

- **Apply material**: select faces → pick a material from the asset/material browser → issues
  an undoable `AssetFaceMaterialCommand` setting `FaceMaterial::Material` on the selected
  faces. Multi-face apply is one command.
- **UV controls** (inspector or a dedicated face panel), each an undoable edit of
  `UvProjection`: scale (texel density), offset (shift), rotation, justify (fit/center/left/
  right/top/bottom — convenience presets computing Offset/Scale from face bounds),
  world/face-aligned toggle.
- **Live preview**: the editor shows the textured face (solid shaded pass, or the PIE bake,
  05-) so the designer sees density/alignment immediately. The face's computed UVs feed a
  simple textured editor material.
- **Default material**: the `LevelDocument` carries a default `AssetRef` (a dev gray material
  that exists in the demo assets) applied to every new face/brush, so a freshly created brush
  is never "no material." Serialized as a level setting.

These reuse the existing command stack + selection + inspector architecture; the only new
data is `FaceMaterial`/`UvProjection` and the commands that edit them.

---

## 3. Material identity & the asset browser (minimum viable)

Faces reference materials by `AssetRef` (`asset://materials/...smat`) — the engine's existing
material asset (Stage 1 of the pipeline: `.smat` JSON, full PBR schema,
`docs/assets/pipeline.md` Decision L). The editor needs to **list available materials** to
pick from:

- Minimum: scan the project's asset root for `.smat` (the engine's `ScanAssetsDirectory`
  already knows materials) and present them in a simple browser/picker panel. No thumbnails
  required for the gate (a textured thumbnail is polish).
- The material picker is the seed of a general asset browser (a forecast editor feature); keep
  it small and material-scoped this branch.

> This is the first time the editor consumes **engine assets** during authoring (vs. only
> editor-only brush data). It loads materials through the same `AssetRef`/`AssetSystem` front
> door the runtime uses — no parallel editor asset path.

---

## 4. Serialization

`FaceMaterial` (material ref + projection) serializes **per face** inside the `brush_meshes`
sidecar (03-§5). Concretely each face object grows:

```json
{ "loop": [0,1,2,3],
  "material": "asset://materials/dev/gray.smat",
  "uv": { "axis_u": [...], "axis_v": [...], "scale": [1,1], "offset": [0,0],
          "rotation": 0, "world_aligned": true } }
```

- Versioned with the brush-mesh section; a face with no `material` falls back to the level
  default (so older/partial data loads cleanly).
- The material `AssetRef` participates in the level's asset set: the cook's manifest walk
  (`CollectAssetPaths`, schema-agnostic per pipeline.md Stage 3) must see these refs so the
  cooked level preloads the materials. Because they live in the brush sidecar (not a standard
  component field), the cook's manifest derivation **must walk the brush sidecar too** — noted
  as an explicit requirement in 05-.

---

## 5. Interaction with the cook (forward reference to 05-)

The cook (05-) consumes `FaceMaterial` + `UvProjection` to produce the runtime mesh:

- **Sections per material**: faces are grouped by their `Material` `AssetRef`; each distinct
  material becomes one `StaticMeshSection` (MaterialSlot). This is *why* the cook is
  material-aware from the start and why "one section, MaterialSlot 0" (the old plan) was
  wrong.
- **UVs are baked at cook time** by evaluating `ProjectUv` per triangulated vertex — the
  authored data stays projection-based (resize-safe), the *cooked* mesh has concrete UVs (the
  runtime never projects). Authored = projection; cooked = baked coordinates. Same
  authored-vs-cooked discipline as everything else.
- Tangents for the cooked vertices are generated from the UVs (MikkTSpace, already in the cook
  per pipeline.md Stage 4c) so normal-mapped materials work on brush geometry.

---

## 6. Stage S5 & gate

Work:
1. `UvProjection`, `FaceMaterial`; attach to `BrushFace`; `ProjectUv`.
2. Face edit mode UV/material commands (apply material, scale/offset/rotate/justify,
   world/face toggle), undoable.
3. Material picker panel (scan `.smat`); level default material setting.
4. Textured face preview in the editor.
5. Serialization of `FaceMaterial` in the brush sidecar; manifest-walk awareness (05-).

**Gate (S5):**
- Apply a material to selected faces; adjust scale/offset/rotation; see it update live.
- **Resize the brush / move a face / extrude — the texture stays pinned (no stretch),
  density constant** for world-aligned; face-aligned follows the face. This is the headline
  requirement, demonstrated.
- Save and re-load: `FaceMaterial`/projection round-trips.
- Pure-function tests: `ProjectUv` correctness (known face → known UVs); resize invariance
  (same projection + moved vertex → continuous UV, constant density); world vs face alignment
  behavior.

---

## 7. Risks & mitigations

- **Seams between adjacent brushes** (texture continuity) — world-aligned projection solves
  the common case; document that face-aligned can seam. Not a blocker.
- **Designer expectation mismatch** with Hammer/ProBuilder specifics (justify semantics,
  texture lock toggles). Mitigation: mirror Hammer's defaults (world-aligned on, texture lock
  on resize) since that is the stated reference.
- **Projection on non-planar faces** is ill-defined. Mitigation: 03- keeps faces planar
  (repair/split); projection assumes planarity and is valid there.
- **Cook manifest missing brush-sidecar material refs** → runtime can't find materials.
  Mitigation: explicit requirement in 05- that manifest derivation walks the brush sidecar;
  tested in the cook integration test.
