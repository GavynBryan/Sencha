// SceneFieldCodec<T> is the per-field hook the generic component serializer
// calls for types whose scene form is not their in-memory form. Asset handles
// are the case that exercises every branch: they serialize as a path, may carry
// a stamped id, and must resolve back through the asset registry on load.

#include <gtest/gtest.h>
#include <render/PointLightComponent.h>
#include <world/transform/TransformComponents.h>

#include <assets/runtime/AssetSystem.h>
#include <core/json/JsonParser.h>
#include <core/logging/LoggingProvider.h>
#include <core/serialization/FourCC.h>
#include <core/serialization/JsonArchive.h>
#include <math/MathSchemas.h>
#include <render/MaterialCache.h>
#include <render/MeshComponentTraits.h>
#include <render/static_mesh/StaticMeshHandle.h>
#include <world/registry/Registry.h>
#include <world/serialization/ComponentSerializer.h>
#include "HandleFieldIo.h"

#include <world/serialization/SceneAssetFieldIo.h>
#include <world/serialization/SceneFieldCodec.h>
#include <world/serialization/SceneSerializer.h>

#include <string_view>
#include <tuple>

struct SceneCodecMaterialComponent
{
    MaterialHandle Material;
};

template <>
struct TypeSchema<SceneCodecMaterialComponent>
{
    static constexpr std::string_view Name = "SceneCodecMaterial";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('T', 'M', 'A', 'T');

    static auto Fields()
    {
        return std::tuple{
            MakeField("material", &SceneCodecMaterialComponent::Material)
                .AsAsset(AssetType::Material),
        };
    }
};

struct SceneCodecMeshComponent
{
    StaticMeshHandle Mesh;
};

template <>
struct TypeSchema<SceneCodecMeshComponent>
{
    static constexpr std::string_view Name = "SceneCodecMesh";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('T', 'M', 'S', 'H');

    static auto Fields()
    {
        return std::tuple{
            MakeField("mesh", &SceneCodecMeshComponent::Mesh)
                .AsAsset(AssetType::StaticMesh),
        };
    }
};

namespace
{
    void RegisterMaterialAsset(AssetRegistry& registry, std::string_view path)
    {
        registry.Register(AssetRecord{
            .Type = AssetType::Material,
            .SourceKind = AssetSourceKind::Procedural,
            .Path = std::string(path),
        });
    }
}

// A zero-size marker: presence is the whole value. The serializer must test
// presence with HasComponent -- a tag has no column, so TryGet answers null
// even when the signature bit is set, and the naive shape silently drops
// every tag from every save.
struct SceneCodecTagComponent
{
};

template <>
struct TypeSchema<SceneCodecTagComponent>
{
    static constexpr std::string_view Name = "SceneCodecTag";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('T', 'T', 'A', 'G');

    static auto Fields() { return std::tuple{}; }
};

TEST(SceneFieldCodec, GenericComponentSerializerWritesTypedMaterialHandleAsPathString)
{
    ComponentSerializerRegistry serializers;
    RegisterComponent<SceneCodecMaterialComponent>(serializers);

    LoggingProvider logging;
    AssetRegistry assetRegistry(logging);
    MaterialCache materials;
    AssetSystem assets(logging, assetRegistry, nullptr, &materials);
    MaterialHandle material = assets.RegisterProceduralMaterial(
        "asset://materials/dev/red.smat",
        Material{ .BaseColor = Vec4(1.0f, 0.0f, 0.0f, 1.0f) });

    Registry registry;
    registry.Components.RegisterComponent<SceneCodecMaterialComponent>();
    EntityId entity = registry.Components.CreateEntity();
    registry.Components.AddComponent(entity, SceneCodecMaterialComponent{ .Material = material });

    SceneSerializationContext context(logging, &assets);
    JsonValue json = SaveSceneJson(registry, serializers, context);

    const JsonValue* entities = json.Find("entities");
    ASSERT_NE(entities, nullptr);
    ASSERT_TRUE(entities->IsArray());
    ASSERT_EQ(entities->AsArray().size(), 1u);

    const JsonValue* components = entities->AsArray()[0].Find("components");
    ASSERT_NE(components, nullptr);
    const JsonValue* component = components->Find("SceneCodecMaterial");
    ASSERT_NE(component, nullptr);
    const JsonValue* materialJson = component->Find("material");
    ASSERT_NE(materialJson, nullptr);
    ASSERT_TRUE(materialJson->IsString());
    EXPECT_EQ(materialJson->AsString(), "asset://materials/dev/red.smat");
}

TEST(SceneFieldCodec, MaterialHandleWritesPathString)
{
    LoggingProvider logging;
    AssetRegistry registry(logging);
    MaterialCache materials;
    AssetSystem assets(logging, registry, nullptr, &materials);
    MaterialHandle handle = assets.RegisterProceduralMaterial(
        "asset://materials/dev/red.smat",
        Material{ .BaseColor = Vec4(1.0f, 0.0f, 0.0f, 1.0f) });

    SceneSerializationContext context(logging, &assets);
    JsonWriteArchive archive;
    ASSERT_TRUE(SaveHandleField<MaterialHandle>(AssetType::Material, archive, "material", handle, context));

    JsonValue json = archive.TakeValue();
    ASSERT_TRUE(json.IsString());
    EXPECT_EQ(json.AsString(), "asset://materials/dev/red.smat");
}

TEST(SceneFieldCodec, MaterialHandleLoadsPathString)
{
    LoggingProvider logging;
    AssetRegistry registry(logging);
    RegisterMaterialAsset(registry, "asset://materials/dev/red.smat");
    MaterialCache materials;
    MaterialHandle registered = materials.Register(
        "asset://materials/dev/red.smat",
        Material{ .BaseColor = Vec4(1.0f, 0.0f, 0.0f, 1.0f) });
    AssetSystem assets(logging, registry, nullptr, &materials);

    auto parsed = JsonParse(R"("asset://materials/dev/red.smat")");
    ASSERT_TRUE(parsed.has_value());

    SceneSerializationContext context(logging, &assets);
    JsonReadArchive archive(*parsed);
    MaterialHandle loaded;
    ASSERT_TRUE(LoadHandleField<MaterialHandle>(AssetType::Material, archive, "", loaded, context));
    EXPECT_EQ(loaded, registered);
}

TEST(SceneFieldCodec, MaterialHandleLoadsLegacyAssetRefObject)
{
    LoggingProvider logging;
    AssetRegistry registry(logging);
    RegisterMaterialAsset(registry, "asset://materials/dev/red.smat");
    MaterialCache materials;
    MaterialHandle registered = materials.Register(
        "asset://materials/dev/red.smat",
        Material{ .BaseColor = Vec4(1.0f, 0.0f, 0.0f, 1.0f) });
    AssetSystem assets(logging, registry, nullptr, &materials);

    auto parsed = JsonParse(R"({ "type": "Material", "path": "asset://materials/dev/red.smat" })");
    ASSERT_TRUE(parsed.has_value());

    SceneSerializationContext context(logging, &assets);
    JsonReadArchive archive(*parsed);
    MaterialHandle loaded;
    ASSERT_TRUE(LoadHandleField<MaterialHandle>(AssetType::Material, archive, "", loaded, context));
    EXPECT_EQ(loaded, registered);
}

TEST(SceneFieldCodec, MaterialIdWinsOverStalePath)
{
    LoggingProvider logging;
    AssetRegistry registry(logging);
    // The asset lives at its post-rename path; the stamped ref still
    // carries the old one. The id must win (Decision A / Stage 4e).
    RegisterMaterialAsset(registry, "asset://materials/dev/renamed.smat");
    ASSERT_TRUE(registry.AssignId("asset://materials/dev/renamed.smat", AssetId{ 0xbeef }));

    MaterialCache materials;
    MaterialHandle registered = materials.Register(
        "asset://materials/dev/renamed.smat",
        Material{ .BaseColor = Vec4(1.0f, 0.0f, 0.0f, 1.0f) });
    AssetSystem assets(logging, registry, nullptr, &materials);

    auto parsed = JsonParse(R"({ "id": "000000000000beef", "path": "asset://materials/dev/old.smat" })");
    ASSERT_TRUE(parsed.has_value());

    SceneSerializationContext context(logging, &assets);
    JsonReadArchive archive(*parsed);
    MaterialHandle loaded;
    ASSERT_TRUE(LoadHandleField<MaterialHandle>(AssetType::Material, archive, "", loaded, context));
    EXPECT_EQ(loaded, registered);
}

TEST(SceneFieldCodec, MaterialHandleFallsBackToPathForUnknownId)
{
    LoggingProvider logging;
    AssetRegistry registry(logging);
    RegisterMaterialAsset(registry, "asset://materials/dev/red.smat");
    MaterialCache materials;
    MaterialHandle registered = materials.Register(
        "asset://materials/dev/red.smat",
        Material{ .BaseColor = Vec4(1.0f, 0.0f, 0.0f, 1.0f) });
    AssetSystem assets(logging, registry, nullptr, &materials);

    auto parsed = JsonParse(R"({ "id": "00000000000dead0", "path": "asset://materials/dev/red.smat" })");
    ASSERT_TRUE(parsed.has_value());

    SceneSerializationContext context(logging, &assets);
    JsonReadArchive archive(*parsed);
    MaterialHandle loaded;
    ASSERT_TRUE(LoadHandleField<MaterialHandle>(AssetType::Material, archive, "", loaded, context));
    EXPECT_EQ(loaded, registered);
}

TEST(SceneFieldCodec, MaterialHandleRejectsMalformedIdAndIdWithoutFallback)
{
    LoggingProvider logging;
    AssetRegistry registry(logging);
    MaterialCache materials;
    AssetSystem assets(logging, registry, nullptr, &materials);
    SceneSerializationContext context(logging, &assets);
    MaterialHandle loaded;

    auto malformed = JsonParse(R"({ "id": "not-hex", "path": "asset://materials/dev/red.smat" })");
    ASSERT_TRUE(malformed.has_value());
    JsonReadArchive malformedArchive(*malformed);
    EXPECT_FALSE(LoadHandleField<MaterialHandle>(AssetType::Material, malformedArchive, "", loaded, context));
    EXPECT_FALSE(malformedArchive.Ok());

    auto idOnly = JsonParse(R"({ "id": "00000000000dead0" })");
    ASSERT_TRUE(idOnly.has_value());
    JsonReadArchive idOnlyArchive(*idOnly);
    EXPECT_FALSE(LoadHandleField<MaterialHandle>(AssetType::Material, idOnlyArchive, "", loaded, context));
    EXPECT_FALSE(idOnlyArchive.Ok());
}

TEST(SceneFieldCodec, MaterialHandleRejectsWrongTypeEmptyPathAndMissingPath)
{
    LoggingProvider logging;
    AssetRegistry registry(logging);
    MaterialCache materials;
    AssetSystem assets(logging, registry, nullptr, &materials);
    SceneSerializationContext context(logging, &assets);

    auto wrongType = JsonParse(R"({ "type": "StaticMesh", "path": "asset://materials/dev/red.smat" })");
    ASSERT_TRUE(wrongType.has_value());
    JsonReadArchive wrongTypeArchive(*wrongType);
    MaterialHandle loaded;
    EXPECT_FALSE(LoadHandleField<MaterialHandle>(AssetType::Material, wrongTypeArchive, "", loaded, context));
    EXPECT_FALSE(wrongTypeArchive.Ok());

    auto emptyPath = JsonParse(R"("")");
    ASSERT_TRUE(emptyPath.has_value());
    JsonReadArchive emptyPathArchive(*emptyPath);
    EXPECT_FALSE(LoadHandleField<MaterialHandle>(AssetType::Material, emptyPathArchive, "", loaded, context));
    EXPECT_FALSE(emptyPathArchive.Ok());

    auto missingPath = JsonParse(R"("asset://materials/dev/missing.smat")");
    ASSERT_TRUE(missingPath.has_value());
    JsonReadArchive missingPathArchive(*missingPath);
    EXPECT_FALSE(LoadHandleField<MaterialHandle>(AssetType::Material, missingPathArchive, "", loaded, context));
    EXPECT_FALSE(missingPathArchive.Ok());
}

TEST(SceneFieldCodec, MaterialHandleRejectsRegistryTypeMismatch)
{
    LoggingProvider logging;
    AssetRegistry registry(logging);
    registry.Register(AssetRecord{
        .Type = AssetType::StaticMesh,
        .SourceKind = AssetSourceKind::Procedural,
        .Path = "asset://materials/dev/red.smat",
    });

    MaterialCache materials;
    [[maybe_unused]] MaterialHandle material = materials.Register(
        "asset://materials/dev/red.smat",
        Material{ .BaseColor = Vec4(1.0f, 0.0f, 0.0f, 1.0f) });
    AssetSystem assets(logging, registry, nullptr, &materials);

    auto parsed = JsonParse(R"("asset://materials/dev/red.smat")");
    ASSERT_TRUE(parsed.has_value());

    SceneSerializationContext context(logging, &assets);
    JsonReadArchive archive(*parsed);
    MaterialHandle loaded;
    EXPECT_FALSE(LoadHandleField<MaterialHandle>(AssetType::Material, archive, "", loaded, context));
    EXPECT_FALSE(archive.Ok());
}

TEST(SceneFieldCodec, StaticMeshHandleRejectsWrongLegacyObjectType)
{
    LoggingProvider logging;
    auto wrongType = JsonParse(R"({ "type": "Material", "path": "asset://meshes/dev/cube.smesh" })");
    ASSERT_TRUE(wrongType.has_value());

    SceneSerializationContext context(logging);
    JsonReadArchive archive(*wrongType);
    StaticMeshHandle loaded;
    EXPECT_FALSE(LoadHandleField<StaticMeshHandle>(AssetType::StaticMesh, archive, "", loaded, context));
    EXPECT_FALSE(archive.Ok());
}

// A scene names the content it is made of, not the processes that read it. A
// dedicated host has no cache that can hold a mesh, so a mesh reference is
// something it declines rather than something it fails on -- and it must
// decline without failing the field, because an invalid field rolls the whole
// scene back and the host would then have no world to simulate.
TEST(SceneFieldCodec, StaticMeshHandleSkipsWhenTheProcessCannotHoldMeshes)
{
    LoggingProvider logging;
    AssetRegistry registry(logging);
    registry.Register(AssetRecord{
        .Type = AssetType::StaticMesh,
        .SourceKind = AssetSourceKind::Procedural,
        .Path = "asset://meshes/dev/cube.smesh",
    });

    MaterialCache materials;
    // No mesh cache: the composition a host without graphics services gets.
    AssetSystem assets(logging, registry, nullptr, &materials);
    ASSERT_FALSE(assets.HasStore(AssetType::StaticMesh));

    auto parsed = JsonParse(R"("asset://meshes/dev/cube.smesh")");
    ASSERT_TRUE(parsed.has_value());

    SceneSerializationContext context(logging, &assets);
    JsonReadArchive archive(*parsed);
    StaticMeshHandle loaded;
    EXPECT_TRUE(LoadHandleField<StaticMeshHandle>(AssetType::StaticMesh, archive, "", loaded, context));
    EXPECT_TRUE(archive.Ok());
    EXPECT_FALSE(loaded.IsValid())
        << "the field is read and declined, leaving no handle behind";
}

// The relaxation is about capability, never about content. Where the process
// can hold the kind, an asset that will not resolve is still a hard failure --
// which is what keeps a client and an editor strict about content they are
// supposed to be able to load.
TEST(SceneFieldCodec, AMissingAssetStillFailsWhereTheProcessCanHoldIt)
{
    LoggingProvider logging;
    AssetRegistry registry(logging);
    MaterialCache materials;
    AssetSystem assets(logging, registry, nullptr, &materials);
    ASSERT_TRUE(assets.HasStore(AssetType::Material));

    // Registered nowhere: the capability exists, the asset does not.
    auto parsed = JsonParse(R"("asset://materials/dev/missing.smat")");
    ASSERT_TRUE(parsed.has_value());

    SceneSerializationContext context(logging, &assets);
    JsonReadArchive archive(*parsed);
    MaterialHandle loaded;
    EXPECT_FALSE(LoadHandleField<MaterialHandle>(AssetType::Material, archive, "", loaded, context));
    EXPECT_FALSE(archive.Ok());
}

// Capability decides nothing about whether a ref is well formed: a malformed
// reference is a broken scene on any process.
TEST(SceneFieldCodec, CapabilityAbsenceDoesNotExcuseAMalformedRef)
{
    LoggingProvider logging;
    AssetRegistry registry(logging);
    MaterialCache materials;
    AssetSystem assets(logging, registry, nullptr, &materials);
    ASSERT_FALSE(assets.HasStore(AssetType::StaticMesh));

    auto parsed = JsonParse(R"({ "type": "Material", "path": "asset://meshes/dev/cube.smesh" })");
    ASSERT_TRUE(parsed.has_value());

    SceneSerializationContext context(logging, &assets);
    JsonReadArchive archive(*parsed);
    StaticMeshHandle loaded;
    EXPECT_FALSE(LoadHandleField<StaticMeshHandle>(AssetType::StaticMesh, archive, "", loaded, context));
    EXPECT_FALSE(archive.Ok());
}

// The whole reason the capability rule exists: a scene rolls back entirely when
// a field is invalid, so on a process that cannot hold meshes a level full of
// them would produce no entities at all -- and therefore no collision, no
// player starts, nothing for a dedicated host to simulate. The entities must
// survive with their meshes simply absent.
TEST(SceneFieldCodec, AMeshBearingSceneStillLoadsWhereMeshesCannotBeHeld)
{
    ComponentSerializerRegistry serializers;
    RegisterEngineSceneSerializers(serializers);
    RegisterComponent<SceneCodecMeshComponent>(serializers);

    auto parsed = JsonParse(R"({
        "version": 1,
        "entities": [
            {
                "components": {
                    "Transform": {
                        "local": {
                            "position": [1.0, 2.0, 3.0],
                            "rotation": [0, 0, 0, 1],
                            "scale": [1, 1, 1]
                        }
                    },
                    "SceneCodecMesh": { "mesh": "asset://meshes/dev/cube.smesh" }
                }
            }
        ]
    })");
    ASSERT_TRUE(parsed.has_value());

    LoggingProvider logging;
    AssetRegistry assetRegistry(logging);
    assetRegistry.Register(AssetRecord{
        .Type = AssetType::StaticMesh,
        .SourceKind = AssetSourceKind::Procedural,
        .Path = "asset://meshes/dev/cube.smesh",
    });
    MaterialCache materials;
    AssetSystem assets(logging, assetRegistry, nullptr, &materials);
    ASSERT_FALSE(assets.HasStore(AssetType::StaticMesh));

    Registry registry;
    registry.Components.RegisterComponent<LocalTransform>();
    registry.Components.RegisterComponent<WorldTransform>();
    registry.Components.RegisterComponent<Parent>();
    registry.Components.RegisterComponent<SceneCodecMeshComponent>();

    SceneSerializationContext context(logging, &assets);
    SceneLoadError error;
    ASSERT_TRUE(LoadSceneJson(*parsed, registry, serializers, context, &error))
        << "scene load error: " << error.Message;

    const std::vector<EntityId> alive = registry.Components.GetAliveEntities();
    ASSERT_EQ(alive.size(), 1u);

    const LocalTransform* transform = registry.Components.TryGet<LocalTransform>(alive[0]);
    ASSERT_NE(transform, nullptr)
        << "the entity and the state a host simulates from must survive";
    EXPECT_FLOAT_EQ(transform->Value.Position.X, 1.0f);

    const SceneCodecMeshComponent* mesh =
        registry.Components.TryGet<SceneCodecMeshComponent>(alive[0]);
    ASSERT_NE(mesh, nullptr);
    EXPECT_FALSE(mesh->Mesh.IsValid()) << "with no mesh behind it";
}

// Display metadata (Field labels/tooltips, enum display names) is editor-only:
// a labeled component must serialize under its persisted field names and load
// back identical, with nothing the labels touched in the archive. The light
// components are the labeled surface, so one of them is the fixture.
TEST(SceneFieldCodec, DisplayMetadataNeverReachesTheArchive)
{
    LoggingProvider logging;
    SceneSerializationContext context(logging);

    PointLightComponent light;
    light.Intensity = 7.5f;
    light.CastShadows = true;
    light.ShadowUpdate = ShadowUpdatePolicy::Static;
    light.BakeContribution = LightBakeContribution::Direct;

    JsonWriteArchive archive;
    ASSERT_TRUE(SceneComponentSerialization::SaveFields(archive, light, context));
    JsonValue json = archive.TakeValue();

    // Persisted keys and enum strings, not the display strings.
    ASSERT_NE(json.Find("bake_contribution"), nullptr);
    EXPECT_EQ(json.Find("bake_contribution")->AsString(), "direct");
    ASSERT_NE(json.Find("shadow_update"), nullptr);
    EXPECT_EQ(json.Find("shadow_update")->AsString(), "static");
    EXPECT_EQ(json.Find("Lighting"), nullptr);
    EXPECT_EQ(json.Find("Shadow Update"), nullptr);

    JsonReadArchive reader(json);
    PointLightComponent loaded;
    ASSERT_TRUE(SceneComponentSerialization::LoadFields(reader, loaded, context));
    EXPECT_FLOAT_EQ(loaded.Intensity, 7.5f);
    EXPECT_TRUE(loaded.CastShadows);
    EXPECT_EQ(loaded.ShadowUpdate, ShadowUpdatePolicy::Static);
    EXPECT_EQ(loaded.BakeContribution, LightBakeContribution::Direct);
}

TEST(SceneFieldCodec, APresentTagComponentSurvivesTheSave)
{
    ComponentSerializerRegistry serializers;
    RegisterComponent<SceneCodecTagComponent>(serializers);

    Registry registry;
    registry.Components.RegisterComponent<SceneCodecTagComponent>();
    const EntityId tagged = registry.Components.CreateEntity();
    registry.Components.AddComponent(tagged, SceneCodecTagComponent{});
    (void)registry.Components.CreateEntity(); // the unmarked control entity

    LoggingProvider logging;
    SceneSerializationContext context(logging);
    const JsonValue json = SaveSceneJson(registry, serializers, context);

    const JsonValue* entities = json.Find("entities");
    ASSERT_NE(entities, nullptr);
    ASSERT_EQ(entities->AsArray().size(), 2u);
    const JsonValue* first = entities->AsArray()[0].Find("components");
    const JsonValue* second = entities->AsArray()[1].Find("components");
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_NE(first->Find("SceneCodecTag"), nullptr);
    EXPECT_EQ(second->Find("SceneCodecTag"), nullptr);
}
