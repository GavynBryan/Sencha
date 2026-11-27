#include <world/identity/PersistableRef.h>

#include <ecs/World.h>
#include <world/identity/PersistentEntityIndex.h>
#include <world/identity/PersistentIdComponent.h>
#include <world/serialization/SceneSerializationContext.h>

#include <algorithm>
#include <string>

PersistableRef MakePersistableRef(const World& world, EntityId entity)
{
    PersistableRef ref{ .Entity = entity, .Target = {} };
    if (world.IsAlive(entity) && world.IsRegistered<PersistentIdComponent>())
        if (const PersistentIdComponent* identity = world.TryGet<PersistentIdComponent>(entity))
            ref.Target = identity->Id;
    return ref;
}

EntityId ResolvePersistableRef(const World& world, const PersistableRef& ref)
{
    if (world.IsAlive(ref.Entity))
        return ref.Entity;
    const PersistentEntityIndex* index = world.TryGetResource<PersistentEntityIndex>();
    const EntityId found = ref.Target.IsValid() && index != nullptr ? index->TryResolve(ref.Target) : EntityId{};
    return world.IsAlive(found) ? found : EntityId{};
}

void RefreshPersistableRef(const World& world, PersistableRef& ref)
{
    ref.Entity = ResolvePersistableRef(world, ref);
}

bool SceneFieldCodec<PersistableRef>::Save(IWriteArchive& archive, std::string_view key, const PersistableRef& value,
                                           SceneSerializationContext&)
{
    const std::string text = value.Target.IsValid() ? PersistentEntityIdToString(value.Target) : std::string{};
    archive.Field(key, std::string_view(text));
    return archive.Ok();
}

bool SceneFieldCodec<PersistableRef>::Load(IReadArchive& archive, std::string_view key, PersistableRef& value,
                                           SceneSerializationContext& context)
{
    std::string text;
    archive.Field(key, text);
    if (!archive.Ok())
        return false;
    value = PersistableRef{};
    if (text.empty())
        return true;
    const auto parsed = PersistentEntityIdFromString(text);
    if (!parsed)
    {
        archive.MarkInvalidField(key);
        return false;
    }
    value.Target = *parsed;
    const std::span<const PackageMember> members = context.PackageMembers;
    const auto member = std::ranges::lower_bound(members, value.Target, {}, &PackageMember::Authored);
    if (member != members.end() && member->Authored == value.Target)
    {
        value.Entity = member->Entity;
        value.Target = member->Carried;
    }
    return true;
}
