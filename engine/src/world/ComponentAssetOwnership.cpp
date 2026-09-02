#include <world/ComponentAssetOwnership.h>

#include <ecs/World.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace
{
    // An asset field is an 8-byte handle, read as the token its store hands
    // out; Field::AsAsset asserts the size.
    std::uint64_t TokenAt(const void* component, const RuntimeField& field)
    {
        std::uint64_t token = 0;
        std::memcpy(&token, static_cast<const std::byte*>(component) + field.Offset, sizeof token);
        return token;
    }
}

void RetainAssetFields(const void* component,
                       std::span<const RuntimeField> fields,
                       World& world)
{
    const AssetStoreTable* stores = world.TryGetResource<AssetStoreTable>();
    if (stores == nullptr)
        return;

    for (const RuntimeField& field : fields)
    {
        if (IAssetStore* store = stores->Find(field.Asset, field.Arity))
            store->RetainToken(TokenAt(component, field));
    }
}

void ReleaseAssetFields(const void* component,
                        std::span<const RuntimeField> fields,
                        World& world)
{
    const AssetStoreTable* stores = world.TryGetResource<AssetStoreTable>();
    if (stores == nullptr)
        return;

    for (auto field = fields.rbegin(); field != fields.rend(); ++field)
    {
        if (IAssetStore* store = stores->Find(field->Asset, field->Arity))
            store->ReleaseToken(TokenAt(component, *field));
    }
}
