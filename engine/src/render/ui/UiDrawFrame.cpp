#include <render/ui/UiDrawFrame.h>

#include <algorithm>

namespace
{
bool SameTexture(const UiTextureRef& a, const UiTextureRef& b)
{
    return a.Kind == b.Kind && a.Content == b.Content && a.Generated == b.Generated && a.Host == b.Host;
}

bool SameClip(const UiClipRect& a, const UiClipRect& b)
{
    return a.Enabled == b.Enabled
        && (!a.Enabled || (a.X == b.X && a.Y == b.Y && a.Width == b.Width && a.Height == b.Height));
}

bool SameCommand(const UiDrawCommand& a, const UiDrawCommand& b)
{
    return a.Geometry == b.Geometry && SameTexture(a.Texture, b.Texture) && a.Translation == b.Translation
        && SameClip(a.Clip, b.Clip) && a.Transform == b.Transform && a.ClipMask == b.ClipMask
        && a.ClipMaskEnabled == b.ClipMaskEnabled && a.Gradient == b.Gradient;
}
} // namespace

bool SameUiRecording(const UiDrawFrame& a, const UiDrawFrame& b)
{
    if (a.Surface.Width != b.Surface.Width || a.Surface.Height != b.Surface.Height)
        return false;
    if (NamesHostImage(a) || NamesHostImage(b))
        return false;
    return std::ranges::equal(a.Commands, b.Commands, SameCommand);
}

bool NamesHostImage(const UiDrawFrame& frame)
{
    return std::ranges::any_of(frame.Commands, [](const UiDrawCommand& command) {
        return command.Texture.Kind == UiTextureKind::Host;
    });
}
