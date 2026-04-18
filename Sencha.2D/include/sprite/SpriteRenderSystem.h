#pragma once

#include <assets/texture/TextureCache.h>
#include <graphics/features/SpriteFeature.h>
#include <math/geometry/2d/Transform2d.h>
#include <registry/Registry.h>
#include <sprite/SpriteStore.h>
#include <transform/TransformStore.h>

class SpriteRenderSystem
{
public:
    SpriteRenderSystem(Registry& registry,
                       SpriteFeature& spriteFeature,
                       TextureCache& textures);
    void Render(float alpha);

private:
    Registry&      GlobalRegistry;
    SpriteFeature& GPUSprites;
    TextureCache&  Textures;
};
