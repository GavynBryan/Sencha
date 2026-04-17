#include <sprite/SpriteRenderSystem.h>

#include <cmath>

SpriteRenderSystem::SpriteRenderSystem(SpriteStore& sprites,
                                       TransformStore<Transform2f>& transforms,
                                       SpriteFeature& spriteFeature,
                                       TextureCache& textures)
    : Sprites(sprites)
    , Transforms(transforms)
    , GPUSprites(spriteFeature)
    , Textures(textures)
{
}

void SpriteRenderSystem::Render(float /*alpha*/)
{
    const std::span<const SpriteComponent> sprites = Sprites.GetItems();
    const std::vector<Id>& owners = Sprites.GetOwners();

    for (size_t i = 0; i < sprites.size(); ++i)
    {
        const SpriteComponent& sprite = sprites[i];
        const Transform2f* world = Transforms.TryGetWorld(EntityHandle{ owners[i], 0 });
        if (!world)
            continue;

        const BindlessImageIndex tex = Textures.GetBindlessIndex(sprite.Texture);

        SpriteFeature::GpuInstance g{};
        g.Center[0]      = world->Position.X;
        g.Center[1]      = world->Position.Y;
        g.HalfExtents[0] = sprite.Width  * 0.5f;
        g.HalfExtents[1] = sprite.Height * 0.5f;
        g.UvMin[0]       = 0.0f;
        g.UvMin[1]       = 0.0f;
        g.UvMax[0]       = 1.0f;
        g.UvMax[1]       = 1.0f;
        g.Color          = sprite.Color;
        g.TextureIndex   = tex.IsValid() ? tex.Value : 0u;
        g.SortKey        = sprite.SortKey;

        if (world->Rotation == 0.0f)
        {
            g.SinRot = 0.0f;
            g.CosRot = 1.0f;
        }
        else
        {
            g.SinRot = std::sin(world->Rotation);
            g.CosRot = std::cos(world->Rotation);
        }

        GPUSprites.SubmitInstance(g);
    }
}
