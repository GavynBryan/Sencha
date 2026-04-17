#pragma once

#include <assets/texture/TextureCache.h>
#include <core/batch/DataBatch.h>
#include <render/features/SpriteFeature.h>
#include <render/components/SpriteComponent.h>

class SpriteRenderSystem
{
public:
    SpriteRenderSystem(DataBatch<SpriteComponent>& sprites,
                       SpriteFeature& spriteFeature,
                       TextureCache& textures);
    void Render(float alpha);

private:
    SpriteFeature&              GPUSprites;
    DataBatch<SpriteComponent>& Sprites;
    TextureCache&               Textures;
};
