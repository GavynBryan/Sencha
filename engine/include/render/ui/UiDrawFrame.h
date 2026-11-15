#pragma once

#include <math/Mat.h>
#include <math/Vec.h>
#include <graphics/RenderExtent.h>
#include <render/TextureHandle.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

//=============================================================================
// UiDrawFrame
//
// One surface's authored UI, as the renderer sees it: ordered draw commands
// over immutable CPU geometry. Render-domain data -- no backend type, no
// document, no handle into anything still being mutated.
//
// Self-contained on purpose. A command holds a reference to its geometry rather
// than an index into a store the runtime is still editing, so consuming a
// submitted frame never reaches back into UI state. That is also what makes CPU
// geometry lifetime a reference-counting question rather than a fence question:
// a blob dies when the last frame naming it does. GPU resources are the ones
// that need GpuFrameRetirement, and they are owned by the feature, not here.
//=============================================================================

// Position in surface pixels, origin top left. Colour is straight-through from
// the document engine: sRGB with PREMULTIPLIED alpha (docs/ui/architecture.md
// §8). Conversion to linear happens before interpolation, never in the fragment
// stage, and alpha is never converted.
struct UiVertex
{
    Vec2d Position;
    Vec2d Uv;
    std::uint8_t ColorSrgbPremultiplied[4] = { 255, 255, 255, 255 };
};

static_assert(sizeof(UiVertex) == 20, "the pass uploads this layout verbatim");

// Built once, never mutated. Shared by every frame that references it.
struct UiGeometryBlob
{
    std::vector<UiVertex> Vertices;
    std::vector<std::uint32_t> Indices;
};

using UiGeometryRef = std::shared_ptr<const UiGeometryBlob>;

// Identity for a texture the document engine generated rather than loaded: a
// glyph atlas, a decorator image. Distinct from an asset handle because the
// lifetime and creation model are different, and disguising one as the other
// would put content-addressed caching in the way of something that has no
// content address (docs/ui/architecture.md §9).
using UiGeneratedTextureId = std::uint32_t;
inline constexpr UiGeneratedTextureId kInvalidUiGeneratedTexture = 0;

// Identity for an image the host supplies at draw time -- a viewport's render,
// a thumbnail, a preview -- which a document names as `host:NAME`. What pixels
// stand behind it is decided by the host each frame, never by the document.
using UiHostImageId = std::uint32_t;
inline constexpr UiHostImageId kInvalidUiHostImage = 0;
inline constexpr std::string_view kUiHostImageScheme = "host:";

enum class UiTextureKind : std::uint8_t
{
    None = 0,
    Content,    // an ordinary asset the open screen holds a lease on
    Generated,  // minted by the document engine, uploaded from UiTextureChanges
    Host,       // supplied by the host at draw time through UiTextureResidency
};

struct UiTextureRef
{
    UiTextureKind Kind = UiTextureKind::None;
    TextureHandle Content{};
    UiGeneratedTextureId Generated = kInvalidUiGeneratedTexture;
    UiHostImageId Host = kInvalidUiHostImage;
};

// Pixels for a generated texture, handed over the first extraction after it was
// minted: RGBA8 with premultiplied alpha. Converted once to linear
// (docs/ui/architecture.md §8), except a gradient's colour ramp, which stays
// sRGB and is decoded where the gradient is drawn (UiGradient).
struct UiTextureUpload
{
    UiGeneratedTextureId Id = kInvalidUiGeneratedTexture;
    RenderExtent Size{};
    std::vector<std::byte> PremultipliedRgba8;
};

// Generated textures minted and finished with since the previous extraction,
// for every surface at once (docs/ui/architecture.md §9).
struct UiTextureChanges
{
    std::vector<UiTextureUpload> Uploads;
    std::vector<UiGeneratedTextureId> Released;
};

// Scissor rectangle in surface pixels. Disabled means "the whole surface".
struct UiClipRect
{
    bool Enabled = false;
    std::int32_t X = 0;
    std::int32_t Y = 0;
    std::int32_t Width = 0;
    std::int32_t Height = 0;
};

// What the clip mask is doing for a command. Rectangular scissor cannot clip to
// a rounded boundary, which is exactly what authored chrome is made of, so the
// stencil-backed mask is part of the supported profile rather than deferred.
enum class UiClipMaskOp : std::uint8_t
{
    None = 0,     // ordinary draw, tested against whatever mask is current
    Set,          // replace the mask with this geometry's coverage
    SetInverse,   // replace it with the area outside this geometry
    Intersect,    // narrow the mask to this geometry
};

// A gradient a command paints instead of a texture: a linear, radial or conic
// gradient decorator. The geometry is the element's background shape, its UV the
// position within the element's box in pixels, so `Origin` and `Axis` are in
// those pixels too.
//
// The colours are a ramp: a row of texels from the first colour stop to the
// last, made once when the document compiles the gradient. Interpolated in
// premultiplied sRGB, as CSS interpolates, so what an author sees in a browser
// is what draws. The ramp stays sRGB because 8-bit linear bands visibly in dark
// gradients; the gradient path decodes each sample.
enum class UiGradientShape : std::uint8_t
{
    Linear = 0,
    Radial,
    Conic,
};

// Texels in a ramp. A stop placed exactly is within half a texel of where it
// was asked for over a gradient this many pixels long.
inline constexpr std::uint32_t kUiGradientRampTexels = 512;

struct UiGradient
{
    UiGradientShape Shape = UiGradientShape::Linear;
    // Past the last stop, start again from the first.
    bool Repeating = false;
    // Linear: the start point. Radial and conic: the centre.
    Vec2d Origin{};
    // Linear: from the start point to the end. Radial: one over the radius on
    // each axis. Conic: the cosine and sine of the angle it starts from.
    Vec2d Axis{};
    // Where along the gradient the first and last stops are, as fractions of
    // its length (radial: of its radius; conic: of a turn). The ramp spans them.
    float FirstStop = 0.0f;
    float LastStop = 1.0f;
    // The colour ramp, kUiGradientRampTexels by 1, premultiplied sRGB.
    UiGeneratedTextureId Ramp = kInvalidUiGeneratedTexture;

    bool operator==(const UiGradient&) const = default;
};

struct UiDrawCommand
{
    UiGeometryRef Geometry;
    UiTextureRef Texture;
    Vec2d Translation{};
    UiClipRect Clip;

    // Set while the document engine has a transform in effect. Absent is the
    // common case and skips the matrix entirely.
    std::optional<Mat4> Transform;

    // When not None, this command writes the clip mask instead of colour.
    UiClipMaskOp ClipMask = UiClipMaskOp::None;

    // Whether colour output is tested against the current mask.
    bool ClipMaskEnabled = false;

    // Set when the command paints a gradient. Its ramp is also `Texture`, so
    // residency and recording comparison treat it like any generated texture.
    std::optional<UiGradient> Gradient;
};

struct UiDrawFrame
{
    // Surface dimensions the commands were laid out against. The pass builds its
    // orthographic transform from this, not from the swapchain, so a surface
    // smaller than the window still maps correctly.
    RenderExtent Surface{};

    // The platform window the surface lives on, 0 for the primary. A window's
    // own render feature draws the frames that name it and nothing else.
    std::uint32_t WindowId = 0;

    // Which of the window's UI render features draws it. A host that draws UI
    // of its own between authored layers stages one feature per layer.
    std::uint8_t Layer = 0;

    std::vector<UiDrawCommand> Commands;

    [[nodiscard]] bool IsEmpty() const { return Commands.empty(); }
};

// Whether two recordings draw the same pixels: the same surface size and the
// same commands over the same geometry. The document engine keeps an element's
// compiled geometry until the element changes, so an unchanged document records
// the same blobs, and comparing them is comparing pointers. Generated texture
// ids are never reused, so the same id is the same pixels. A host image is
// whatever its host published this frame, so a recording naming one is never
// the same as another (`NamesHostImage`).
[[nodiscard]] bool SameUiRecording(const UiDrawFrame& a, const UiDrawFrame& b);
[[nodiscard]] bool NamesHostImage(const UiDrawFrame& frame);

// A Texture-destined surface's recording and the target texture it draws into
// (render/TargetTextures.h). Published every frame the surface lives, empty or
// not, so the feature drawing it can tell a surface that drew nothing this
// frame, whose texture it clears, from one that is gone, whose texture it
// withdraws.
struct UiTextureFrame
{
    std::string Texture;
    UiDrawFrame Frame;
};
