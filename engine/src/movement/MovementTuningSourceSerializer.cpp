#include <movement/MovementTuningSourceSerializer.h>

#include <assets/runtime/AssetSystem.h>
#include <core/assets/AssetLease.h>
#include <core/logging/LoggingProvider.h>
#include <core/metadata/RuntimeSchema.h>
#include <core/serialization/FourCC.h>
#include <ecs/ComponentTypeId.h>
#include <movement/MovementComponentSchemas.h>
#include <world/serialization/IComponentSerializer.h>
#include <world/serialization/SceneAssetRef.h>

#include <cstddef>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    constexpr std::string_view kProfileKey = "profile";

class MovementTuningSourceSerializer final : public IComponentSerializer
{
public:
    ComponentTypeId TypeId() const override
    {
        return ResolveComponentTypeId<MovementTuningSource>();
    }

    std::string_view JsonKey() const override { return "MovementTuning"; }
    std::uint32_t BinaryChunkId() const override
    {
        return MakeFourCC('M', 'T', 'U', 'N');
    }

    // The profile is a describable member: an asset handle at a known offset,
    // exactly the shape an inspector already resolves through the asset system.
    // The persisted form is still the path this serializer writes, not the
    // session-local handle these bytes hold.
    std::span<const RuntimeField> RuntimeFields() const override
    {
        return RuntimeFieldsOf<MovementTuningSource>();
    }

    std::vector<std::byte> DefaultBytes() const override
    {
        MovementTuningSource value{};
        std::vector<std::byte> bytes(sizeof(MovementTuningSource));
        std::memcpy(bytes.data(), &value, sizeof(MovementTuningSource));
        return bytes;
    }

    void RegisterStorage(Registry& registry) const override
    {
        if (!registry.Components.IsRegistered<MovementTuningSource>())
            registry.Components.RegisterComponent<MovementTuningSource>();
    }

    bool HasComponent(EntityId entity, const Registry& registry) const override
    {
        return registry.Components.IsRegistered<MovementTuningSource>()
            && registry.Components.HasComponent<MovementTuningSource>(entity);
    }

    bool Save(IWriteArchive& archive,
              EntityId entity,
              const Registry& registry,
              SceneSerializationContext& context) const override
    {
        if (!registry.Components.IsRegistered<MovementTuningSource>())
            return true;

        const MovementTuningSource* tuning =
            registry.Components.TryGet<MovementTuningSource>(entity);
        if (tuning == nullptr)
            return true;

        archive.BeginObject(std::string_view{});

        // An unresolved profile writes nothing rather than an empty path: the
        // absence is what a character on default coefficients means, and it
        // reads back as the same thing.
        if (context.Assets != nullptr && tuning->Profile.IsValid())
        {
            const std::string_view path = context.Assets->GetPathForLease(
                AssetType::Data, tuning->Profile.Value.ToToken());
            if (!WriteSceneAssetRef(archive, kProfileKey, path, context))
                return false;
        }

        archive.End();
        return archive.Ok();
    }

    bool Load(IReadArchive& archive,
              EntityId entity,
              Registry& registry,
              SceneSerializationContext& context) override
    {
        return LoadIntoWorld(archive, entity, registry.Components, context);
    }

    bool LoadIntoWorld(IReadArchive& archive,
                       EntityId entity,
                       World& world,
                       SceneSerializationContext& context) override
    {
        // The reference reads in whichever shape it was written: a bare path
        // from authoring, or the {id, path} the cook stamps.
        archive.BeginObject(std::string_view{});
        std::string profilePath;
        const bool stated = archive.HasField(kProfileKey);
        const bool read = !stated
            || ReadSceneAssetRef(archive, kProfileKey, AssetType::Data, profilePath,
                                 context);
        archive.End();
        if (!read || !archive.Ok())
            return false;

        MovementTuningSource tuning{};

        // The lease holds the profile across the add; the entity's copy takes
        // its own reference through OnAdd, and this one goes at the end of the
        // scope. A profile that will not load leaves the handle invalid, which
        // is a character on default coefficients rather than a refused scene.
        // A stated profile with no asset system to resolve it is a load that
        // would silently drop authored content -- and, on a cook, write the
        // dropped value back out. Refused rather than skipped, exactly as an
        // asset-handle field is.
        if (!profilePath.empty() && context.Assets == nullptr)
        {
            context.Logging->GetLogger<SceneSerializationContext>().Error(
                "MovementTuningSource: no asset system to resolve profile '{}'",
                profilePath);
            archive.MarkInvalidField(kProfileKey);
            return false;
        }

        AssetLease profile;
        if (!profilePath.empty() && context.Assets != nullptr
            && context.Assets->HasStore(AssetType::Data))
        {
            profile = context.Assets->LoadLease(profilePath, AssetType::Data);
            if (profile.IsValid())
            {
                tuning.Profile.Value =
                    DataAssetHandle::FromToken(profile.OpaqueToken());
            }
            else
            {
                context.Logging->GetLogger<SceneSerializationContext>().Warn(
                    "MovementTuningSource: movement profile '{}' did not load; "
                    "the character moves on default coefficients",
                    profilePath);
            }
        }

        if (world.HasComponent<MovementTuningSource>(entity))
            return world.InitializeComponent<MovementTuningSource>(entity, tuning);

        world.AddComponent<MovementTuningSource>(entity, tuning);
        return true;
    }

    bool Remove(EntityId entity, Registry& registry) const override
    {
        if (registry.Components.IsRegistered<MovementTuningSource>()
            && registry.Components.HasComponent<MovementTuningSource>(entity))
        {
            registry.Components.RemoveComponent<MovementTuningSource>(entity);
        }
        return true;
    }
};
} // namespace

std::unique_ptr<IComponentSerializer> MakeMovementTuningSourceSerializer()
{
    return std::make_unique<MovementTuningSourceSerializer>();
}
