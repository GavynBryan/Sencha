#pragma once

#include <math/Mat.h>
#include <math/Vec.h>
#include <graphics/RenderExtent.h>
#include <render/TextureHandle.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
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

enum class UiTextureKind : std::uint8_t
{
    None = 0,
    Content,    // an ordinary asset the open screen holds a lease on
    Generated,  // minted by the document engine, uploaded from PendingUploads
};

struct UiTextureRef
{
    UiTextureKind Kind = UiTextureKind::None;
    TextureHandle Content{};
    UiGeneratedTextureId Generated = kInvalidUiGeneratedTexture;
};

// Pixels for a generated texture, carried on the first frame that uses it. RGBA8
// with premultiplied alpha, already converted to linear (§8): the document
// engine hands over sRGB, and converting once here beats converting per sample.
struct UiTextureUpload
{
    UiGeneratedTextureId Id = kInvalidUiGeneratedTexture;
    RenderExtent Size{};
    std::vector<std::byte> LinearPremultipliedRgba8;
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
};

struct UiDrawFrame
{
    // Surface dimensions the commands were laid out against. The pass builds its
    // orthographic transform from this, not from the swapchain, so a surface
    // smaller than the window still maps correctly.
    RenderExtent Surface{};

    std::vector<UiDrawCommand> Commands;

    // Generated textures first used this frame, and ones the document engine
    // has finished with. The feature owns the GPU side of both.
    std::vector<UiTextureUpload> PendingUploads;
    std::vector<UiGeneratedTextureId> ReleasedTextures;

    [[nodiscard]] bool IsEmpty() const
    {
        return Commands.empty() && PendingUploads.empty() && ReleasedTextures.empty();
    }
};
