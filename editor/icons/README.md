# Editor icons

One SVG per `IconId` (`editor/common/src/icons/IconId.h`), named by the id in
kebab case (`grid-frame.svg` for `IconId::GridFrame`). The editor reads them at
startup and rasterizes them into its font atlas at a few sizes, so a change to a
file shows on the next launch. They are editor data like the fonts and themes,
not registry assets.

Convention: `viewBox="0 0 24 24"`; shapes in white (`#ffffff`), which the
chrome tints with the control's state at draw time; strokes and fills only, no
text or gradients. An icon whose file is missing or blank falls back to a Font
Awesome glyph.

The rasterizer is nanosvg (zlib license), fetched by `editor/common/CMakeLists.txt`.
