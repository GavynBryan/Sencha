#pragma once

class TextureCache;

//=============================================================================
// Where a zone's baked-lighting atlases live, so ZoneLightmapComponent's
// lifecycle hooks can hold what they name. A host points this at its texture
// cache; null in a world composed without one, where the hooks then do nothing.
//=============================================================================
struct ZoneLightmapComponentAssets
{
    ZoneLightmapComponentAssets() = default;
    explicit ZoneLightmapComponentAssets(TextureCache* textures)
        : Textures(textures)
    {
    }

    TextureCache* Textures = nullptr;
};
