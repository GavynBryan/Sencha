#include <sprite/SpriteRenderSystem.h>

#include <cmath>
#include <registry/FrameRegistryView.h>

SpriteRenderSystem::SpriteRenderSystem(Registry& registry,
                                       SpriteFeature& spriteFeature,
                                       TextureCache& textures)
    : GlobalRegistry(registry)
    , GPUSprites(spriteFeature)
    , Textures(textures)
{
}

void SpriteRenderSystem::Render(float /*alpha*/)
{
    Registry* visibleRegistries[] = { &GlobalRegistry };
    FrameRegistryView view;
    view.Global = &GlobalRegistry;
    view.Visible = visibleRegistries;

    for (Registry* registry : view.Visible)
    {
        auto* spritesStore = registry->Components.TryGet<SpriteStore>();
        auto* transforms = registry->Components.TryGet<TransformStore<Transform2f>>();

        if (!spritesStore || !transforms)
            continue;

        const std::span<const SpriteComponent> sprites = spritesStore->GetItems();
        const std::vector<Id>& owners = spritesStore->GetOwners();

        for (size_t i = 0; i < sprites.size(); ++i)
        {
            const SpriteComponent& sprite = sprites[i];
            const Transform2f* world = transforms->TryGetWorld(EntityId{ owners[i], 0 });
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
}
