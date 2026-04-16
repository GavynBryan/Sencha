#include <render/SpriteRenderSystem.h>

SpriteRenderSystem::SpriteRenderSystem(DataBatch<SpriteComponent>& sprites,
                                       SpriteFeature& spriteFeature,
                                       TextureCache& textures)
    : Sprites(sprites)
    , GPUSprites(spriteFeature)
    , Textures(textures)
{
}

void SpriteRenderSystem::Render(float /*alpha*/)
{
    for (const SpriteComponent& sprite : Sprites.GetItems())
    {
        const Transform2f& world = sprite.Transform.World();

        GPUSprites.Submit({
            .CenterX  = world.Position.X,
            .CenterY  = world.Position.Y,
            .Width    = sprite.Width,
            .Height   = sprite.Height,
            .Color    = sprite.Color,
            .Texture  = Textures.GetBindlessIndex(sprite.Texture),
            .Rotation = world.Rotation,
            .SortKey  = sprite.SortKey,
        });
    }
}
