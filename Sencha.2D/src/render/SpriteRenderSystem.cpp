#include <render/SpriteRenderSystem.h>

#include <cmath>

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
        const BindlessImageIndex tex = Textures.GetBindlessIndex(sprite.Texture);

        SpriteFeature::GpuInstance g{};
        g.Center[0]      = world.Position.X;
        g.Center[1]      = world.Position.Y;
        g.HalfExtents[0] = sprite.Width  * 0.5f;
        g.HalfExtents[1] = sprite.Height * 0.5f;
        g.UvMin[0]       = 0.0f;
        g.UvMin[1]       = 0.0f;
        g.UvMax[0]       = 1.0f;
        g.UvMax[1]       = 1.0f;
        g.Color          = sprite.Color;
        g.TextureIndex   = tex.IsValid() ? tex.Value : 0u;
        g.SortKey        = sprite.SortKey;

        if (world.Rotation == 0.0f)
        {
            g.SinRot = 0.0f;
            g.CosRot = 1.0f;
        }
        else
        {
            g.SinRot = std::sin(world.Rotation);
            g.CosRot = std::cos(world.Rotation);
        }

        GPUSprites.SubmitInstance(g);
    }
}
