#pragma once

#include <assets/texture/TextureCache.h>
#include <graphics/features/SpriteFeature.h>
#include <math/geometry/2d/Transform2d.h>
#include <sprite/SpriteComponent.h>
#include <transform/TransformStore.h>

class SpriteRenderSystem
{
public:
    SpriteRenderSystem(SpriteStore& sprites,
                       TransformStore<Transform2f>& transforms,
                       SpriteFeature& spriteFeature,
                       TextureCache& textures);
    void Render(float alpha);

private:
    SpriteStore&                Sprites;
    TransformStore<Transform2f>& Transforms;
    SpriteFeature&              GPUSprites;
    TextureCache&               Textures;
};
