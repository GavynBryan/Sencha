#include <runtime/spawn/SceneSpawnOverlay.h>

#include <core/logging/Logger.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/World.h>
#include <world/build/EntityBuildPackage.h>
#include <world/identity/PersistentIdComponent.h>
#include <world/serialization/EntityPatch.h>
#include <world/serialization/SceneSerializationContext.h>

#include <optional>

namespace
{
    constexpr ComponentPersistenceSet kBodySaves{ .Saved = true };

    [[nodiscard]] std::optional<PackageEntityId> OnlyRoot(const EntityBuildPackage& package)
    {
        std::optional<PackageEntityId> root;
        const auto entities = package.Entities();
        for (std::uint32_t i = 0; i < entities.size(); ++i)
        {
            if (entities[i].HasParent)
                continue;
            if (root.has_value())
                return std::nullopt;
            root = PackageEntityId{ i };
        }
        return root;
    }
}

SceneSpawnOverlay CaptureSpawnOverlay(const World& world, EntityId root, const EntityBuildPackage& prefab,
                                      const ComponentSerializerRegistry& serializers,
                                      SceneSerializationContext& context)
{
    SceneSpawnOverlay overlay;
    if (const PersistentIdComponent* id = world.IsRegistered<PersistentIdComponent>()
            ? world.TryGet<PersistentIdComponent>(root)
            : nullptr)
        overlay.RootId = id->Id;
    const std::optional<PackageEntityId> prefabRoot = OnlyRoot(prefab);
    const std::vector<AuthoredComponent> baseline = prefabRoot.has_value()
        ? AuthoredComponentsOf(prefab.Entities()[prefabRoot->Value], serializers, { .Declared = kBodySaves })
        : std::vector<AuthoredComponent>{};
    std::vector<std::string> failed;
    overlay.RootPatch = CaptureEntityPatch(world, root, baseline, kBodySaves, serializers, context, nullptr, &failed);
    for (const std::string& key : failed)
        context.Logging->GetLogger<SceneSpawnOverlay>().Error(
            "save: '{}' could not be written; the body keeps its prefab's", key);
    return overlay;
}

bool ApplySpawnOverlay(EntityBuildPackage& package, const SceneSpawnOverlay& overlay,
                       const ComponentSerializerRegistry& serializers, std::string& error,
                       std::vector<std::string>& retuned)
{
    if (!overlay.RootPatch.IsObject() || overlay.RootPatch.Size() == 0)
        return true;
    const std::optional<PackageEntityId> root = OnlyRoot(package);
    if (!root.has_value())
    {
        error = "the scene has not exactly one root to give its saved state to";
        return false;
    }
    EntityPatchOutcome outcome = ApplyEntityPatch(*package.FindEntity(*root), overlay.RootPatch, serializers);
    const auto refuse = [&error](const std::string& reason) { error += (error.empty() ? "" : "; ") + reason; };
    for (const std::string& key : outcome.Unknown)
        refuse("the saved state names component '" + key + "', which this build does not have");
    if (!outcome.Misfit.empty())
    {
        std::string misfit = "the saved state no longer fits its scene: " + outcome.Misfit.front();
        for (std::size_t i = 1; i < outcome.Misfit.size(); ++i)
            misfit += "; " + outcome.Misfit[i];
        refuse(misfit);
    }
    retuned.insert(retuned.end(), std::make_move_iterator(outcome.Retuned.begin()),
                   std::make_move_iterator(outcome.Retuned.end()));
    return error.empty();
}
