# Editor UI Look-and-Feel

Status: plan (not yet implemented). Target captured from a concept mockup shared
2026-06-17. See memory `editor-ui-look-and-feel`.

## Context

The editor's chrome is currently ImGui's stock `StyleColorsDark()` (set in
`EditorUiFeature`), with one bespoke touch (per-severity console text color). The
panels exist and are functional (viewport, world hierarchy, inspector, mesh-edit,
tool palette, console) but look generic — the user's words: "ugly and clunky."

The target is a dense, dark **sci-fi / cyberpunk** editor in the spirit of Source 2
Hammer / Unreal: charcoal-navy panels, **teal/cyan accents**, amber/orange
highlights, thin 1px borders, low rounding, a crisp techy font with icon glyphs,
an icon toolbar, a status bar, thumbnail asset browser, component-sectioned
inspector, severity-colored console.

## Guiding principles

1. **Chrome, not the viewport scene.** The mockup's gorgeous lit level is the
   renderer + art, not UI. This plan is about ImGui chrome only; the viewport
   renders whatever is in the level.
2. **Theme is data.** Colors/metrics live in one styled seam, not scattered
   `PushStyleColor` calls. One place to retune the whole editor.
3. **Achievable-first.** ImGui reaches ~80% of the feel cheaply (palette, fonts,
   icons, metrics, status bar, colored console, list/section widgets). The last
   ~20% (beveled/angled panel frames, glow) needs custom per-panel draw-list
   chrome — expensive, fragile, diminishing returns; deferred/optional.
4. **Separate chrome from features.** Several mockup elements imply *new data or
   features*, not just styling (see "Chrome vs. feature" below). Style what we
   have; flag the rest as dependencies, don't fake them.

## Architecture discipline (no debt, no indirection)

This pass must preserve what makes the editor modular: the UI is a *view over
registries and state*, behavior is decoupled, and there are no central switch
statements that name specific things. Concretely:

1. **Theme is flat leaf data, not a framework.** `EditorUiStyle` is a palette
   struct + an `Apply`/`LoadFonts` function. No theming engine, no runtime style-
   token registry, no observers, no hot-swap, no skin files. The moment it grows
   indirection it has failed. (Mirror of `EditorTheme.h` for overlays.)
2. **One palette, zero literals.** Every color/metric flows from the palette;
   panels reference named constants. No `PushStyleColor` with raw values (retire
   the existing one in `MeshEditPanel`). Enforce mechanically with a fitness
   function: grep `editor/ui` for `ImVec4(`/`IM_COL32(` color literals outside
   `EditorUiStyle` (sibling to the dependency/ABI checks).
3. **Chrome stays a view over registries/state — no hardcoded enumerations.**
   - The toolbar renders from `ToolRegistry`, not a hand-written tool list.
   - A tool advertises its own display (label + icon glyph) as **data on the
     tool** (an `ITool` display hint), exactly like `EditorVisual` sits on a
     component descriptor — so the toolbar names no specific tool and a new tool
     gets a button for free.
   - Snap/grid/angle toggles and the status bar **read/write existing settings**;
     they own no behavior of their own.
4. **Build only feature-backed chrome.** No dead buttons. If the mockup element
   needs data we don't have (Rotate/Scale, visibility/lock, materials,
   thumbnails, non-box shapes), it waits for that feature — the chrome-vs-feature
   table is the contract.
5. **Theme is confined to `editor/ui`.** No theme/ImGui dependency leaks into
   `meshedit`/`editmodes`/engine; the existing dependency fitness functions keep
   holding.
6. **Reuse, don't reinvent.** One small home for any shared widget helper
   (icon-button, swatch, section header) rather than copy-paste across panels —
   the `EditorLinePipeline`/`ViewportProjection` consolidation instinct.

## Palette (sRGB float; the single source for the ImGui style)

Tuned dark — a "dead CRT / cursed Winamp skin" (per the second mockup): near-black
gunmetal chrome, a *muted* teal accent (not a glowing cyan), amber + lime VU
secondaries. The first-pass values glowed too bright on a dark monitor; backgrounds
now sit at single-digit-% lightness so the editor recedes and the viewport carries
the color.

```
Background (window)     #060A0A   (0.024, 0.031, 0.039)  app void, near black
Panel / child bg        #0A0D0F   (0.039, 0.051, 0.059)  panel gunmetal
Header / title bg       #0E1215   (0.055, 0.071, 0.082)  titles/menus, raised
Frame bg (inputs)       #040608   (0.016, 0.024, 0.031)  inset wells, darkest
Frame bg hovered        #10171B   (0.063, 0.090, 0.106)
Frame bg active         #17212700 (0.090, 0.129, 0.153)
Border                  #1C262B   (0.110, 0.149, 0.169)  hairline steel
Accent (teal)           #239AAB   (0.137, 0.604, 0.671)  muted; selection/active/check
Accent hover            #38B8C9   (0.220, 0.722, 0.788)
Accent dim (idle bar)   #145660   (0.078, 0.337, 0.376)
Selected (row)          #123037   (0.071, 0.188, 0.216)
Warning / amber         #D98E26   (0.851, 0.557, 0.149)  warnings, "Locked"
Danger / red            #CA463C   (0.792, 0.275, 0.235)  "Locked" red
Critical / magenta      #E33277   (0.890, 0.196, 0.467)
Success / lime          #6BBE4A   (0.420, 0.745, 0.290)  VU green
Text primary            #B8C4CA   (0.722, 0.769, 0.792)
Text dim / disabled     #677380   (0.404, 0.451, 0.482)
```

These also supply the console severity colors (info=text, warn=amber, error=red,
success=green) and the Entities-list status colors (Active=green, Locked=amber,
Idle=dim).

## Metrics (ImGuiStyle)

```
WindowRounding 2   ChildRounding 2   FrameRounding 2   GrabRounding 2
TabRounding 2      ScrollbarRounding 2   PopupRounding 2
WindowBorderSize 1 FrameBorderSize 1     TabBorderSize 0
WindowPadding (8,8)  FramePadding (7,4)   ItemSpacing (8,5)
ItemInnerSpacing (6,4)  IndentSpacing 18   ScrollbarSize 12   GrabMinSize 10
```
Squared, thin-bordered, tight — the sci-fi panel read.

## Fonts — DONE (sourced + wired)

Bundled in `editor/fonts/` (all permissively licensed; see that dir's `README.md`
+ `LICENSE-*.txt`). All four TTFs verified to carry a `glyf` table so ImGui's
`stb_truetype` rasterizes them directly (no variable-font / CFF surprises):

- **UI font:** Inter 4.1 Regular @ 15px (SIL OFL 1.1) — the default font.
- **Monospace:** JetBrains Mono Regular @ 14px (SIL OFL 1.1) — console/readouts,
  exposed via `EditorUi::MonoFont()`.
- **Icons:** Font Awesome 6 Free Solid @ 14px (OFL fonts / CC-BY 4.0 designs)
  merged into the UI font via `IconsFontAwesome6.h` (`ICON_FA_*` macros), so icon
  glyphs render inline in labels — gates the Phase 2 icon toolbar.

Loading: `EditorUi::LoadFonts(io)` builds the atlas, called right after
`EditorUi::Apply` in `EditorUiFeature::InitImGui`. Files resolve via the
`SENCHA_EDITOR_FONT_DIR` compile-def (parallels `SENCHA_EDITOR_ASSET_DIR`), and a
missing TTF falls back to ImGui's built-in font rather than failing.

## ImGui color mapping (key entries)

```
WindowBg/ChildBg        -> Panel bg
TitleBg/TitleBgActive   -> Header bg
MenuBarBg               -> Header bg
FrameBg/Hovered/Active  -> Frame bg variants
Border                  -> Border
Button                  -> Frame bg ; Hovered -> Frame hovered ; Active -> Accent dim
Header/Hovered/Active    -> Accent dim/accent (selection rows, collapsing headers)
CheckMark/SliderGrab     -> Accent
Tab/TabHovered/TabActive -> Header bg / Accent dim / Accent
Separator/Resize         -> Border / Accent on hover
Text / TextDisabled      -> Text primary / dim
```

## Layout / widget inventory

Mapped to the mockup, marking what exists, what's new chrome, and what implies a
feature/data dependency.

| Mockup element | Status |
| --- | --- |
| Top menu bar (File/Edit/View/…) | exists (EditorUiFeature menu) — restyle |
| Icon **toolbar**: Select/Move/Rotate/Scale + Grid/Snap/Angles/Bounds/… | **new chrome**; Move/Bounds map to current manipulators, Rotate/Scale are future features |
| World **Outliner** tree | exists (SceneHierarchyPanel) — restyle; per-row **eye/lock** ⇒ needs visibility/lock state in the scene (**feature**) |
| **Brush Tools** palette (shape icons) | exists (ToolPalettePanel, text) — iconify; non-box shapes are future kernel **features** |
| **Materials** swatches | **new**; needs material assets + swatch rendering (**feature**) |
| Viewport **tabs** (Persp/Top/Front/Side/Lighting/Portals) | four-way layout exists; tabbed single-view is a layout option — restyle |
| **Asset Browser** thumbnails | **new**; needs a thumbnail-render pipeline (**feature**) |
| **Console** severity log | exists with coloring — restyle to palette |
| **Inspector** component sections | exists (registry-driven) — restyle/group |
| **Entities** list w/ status colors | partly (hierarchy); status colors are chrome |
| **Status bar** (GRID/SNAP/ANGLES/MEM/clock) | **new chrome**; values exist (grid/snap, mem) |

## The style seam

Add `editor/ui/EditorUiStyle.{h,cpp}`:
- `void ApplyEditorStyle(ImGuiStyle&)` — palette + metrics (replaces
  `StyleColorsDark()` in `EditorUiFeature`).
- `void LoadEditorFonts(ImGuiIO&)` — UI + mono + icon atlas.
- Palette exposed as named `ImVec4` constants reused by panels (console severity,
  status colors) instead of literals — the 2D analog of `EditorTheme.h`.

## Phases

1. **Theme foundation** — `EditorUiStyle` (palette + metrics) replacing
   `StyleColorsDark`, + fonts/icons. Console + mesh-edit ad-hoc colors repointed
   to the palette. *Biggest jump, lowest risk.*
2. **Toolbar + status bar** — DONE. `EditorToolbar` (top side bar): icon buttons
   for the registered tools (driving `ToolRegistry`) + the mesh element modes
   Object/Vertex/Edge/Face (driving `MeshEditService`), each with an accent
   active-highlight; icons come from a new `ITool::GetIcon()` (`ICON_FA_*`).
   `EditorStatusBar` (bottom side bar): read-only active tool, selection count,
   active-viewport orientation + grid spacing, wall clock. Both are fixed chrome
   drawn via `BeginViewportSideBar` (reserves work-area space the full-bleed
   viewport panel avoids), registered through `EditorUiFeature::AddChrome`.
   *Deliberately no snap/grid/angle **toggles**: those imply backing state
   (snap-enable, configurable spacing, angle snap) the editor doesn't have yet —
   they'd be fake buttons, so they're deferred to Phase 4 as a real feature.*
3. **Panel polish** — DONE (the parts not gated on backing data). Inspector:
   two-column `[label | widget]` field layout (widgets fill the row, labels in an
   aligned left column) replacing ImGui's default label-after-widget look;
   icon'd collapsing component sections; icon'd entity header + Add Component
   button. Outliner: leading per-row glyph (cube for populated entities, dot for
   empty), still fully registry-driven. *Deferred to Phase 4 (need backing state,
   would be fake otherwise): entity **status colors** (Active/Locked/Idle),
   per-row **eye/lock** toggles, and the **tabbed** single-viewport layout (a
   layout feature, not styling — the four-way layout already works).*
4. **Feature-backed elements** — visibility/lock state, material swatches, asset
   thumbnails (each its own scoped feature, gated on backing data).
5. *(Optional, expensive)* custom beveled/glow chrome via draw-list, only if
   pixel-fidelity is wanted.

## Risks / honest ceiling

- Beveled/angled frames + glow aren't native ImGui — custom draw-list per panel,
  high effort. Treat the squared-bordered theme as the realistic target.
- Asset thumbnails, material swatches, and per-entity visibility/lock are
  **features** (need render/scene support), not styling — they gate their mockup
  elements.
- Fonts/icons need sourced TTF assets (licensing) and atlas wiring first.
- The viewport's lit scene is renderer/art, out of scope here.

## Done definition (Phases 1–3, the chrome the user reacted to)

1. Editor opens in the custom dark-teal theme (no stock ImGui look).
2. Crisp UI font + icon glyphs in use.
3. Icon toolbar (tool select + snap/grid/angle toggles) and bottom status bar.
4. Console, inspector, outliner, entities styled to the palette with status colors.
5. All colors flow from `EditorUiStyle`'s palette — no scattered literals.
