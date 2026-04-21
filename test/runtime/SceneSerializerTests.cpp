#include <gtest/gtest.h>

#include <core/assets/AssetSystem.h>
#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <core/serialization/JsonArchive.h>
#include <core/serialization/BinaryFormat.h>
#include <core/serialization/BinaryReader.h>
#include <core/serialization/BinaryWriter.h>
#include <core/serialization/Serialize.h>
#include <core/logging/LoggingProvider.h>
#include <math/geometry/3d/Transform3d.h>
#include <render/Camera.h>
#include <render/MaterialCache.h>
#include <render/MeshRendererComponent.h>
#include <render/static_mesh/StaticMeshHandle.h>
#include <world/registry/Registry.h>
#include <world/serialization/SceneFormat.h>
#include <world/serialization/SceneFieldCodec.h>
#include <world/serialization/SceneSerializer.h>
#include <world/transform/TransformHierarchyService.h>
#include <world/transform/TransformPropagationOrderService.h>
#include <world/transform/TransformStore.h>

#include <sstream>
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

    static auto Fields()
    {
        return std::tuple{
            MakeField("material", &SceneCodecMaterialComponent::Material),
        };
    }
};

template <>
struct ComponentStorageTraits<SceneCodecMaterialComponent>
{
    using Store = SparseSetStore<SceneCodecMaterialComponent>;
    static constexpr std::uint32_t BinaryChunkId = MakeFourCC('T', 'M', 'A', 'T');

    static bool Add(Registry& registry, EntityId entity, SceneCodecMaterialComponent component)
    {
        return registry.Components.Ensure<Store>().Add(entity, component);
    }
};

namespace
{
    std::stringstream MakeBinaryStream()
    {
        return std::stringstream(std::ios::in | std::ios::out | std::ios::binary);
    }

    Registry MakeSceneRegistry()
    {
        Registry registry;
        auto& order = registry.Resources.Register<TransformPropagationOrderService>();
        registry.Resources.Register<TransformHierarchyService>();
        registry.Components.Register<TransformStore<Transform3f>>(order);
        registry.Components.Register<MeshRendererStore>();
        registry.Components.Register<CameraStore>();
        return registry;
    }

    void ResetSceneSerializers()
    {
        ClearComponentSerializers();
        InitSceneSerializer();
    }

    void RegisterMaterialAsset(AssetRegistry& registry, std::string_view path)
    {
        registry.Register(AssetRecord{
            .Type = AssetType::Material,
            .SourceKind = AssetSourceKind::Procedural,
            .Path = std::string(path),
        });
    }

    Transform3f MakeTransform(float x, float y, float z)
    {
        return Transform3f(
            Vec3d(x, y, z),
            Quatf::Identity(),
            Vec3d(1.0f, 2.0f, 3.0f));
    }
}

TEST(SceneSerializer, BinaryRoundTripsCleanRegistry)
{
    ResetSceneSerializers();
    Registry source = MakeSceneRegistry();

    EntityId parent = source.Entities.Create();
    EntityId child = source.Entities.Create();

    auto& transforms = source.Components.Get<TransformStore<Transform3f>>();
    auto& hierarchy = source.Resources.Get<TransformHierarchyService>();
    transforms.Add(parent, MakeTransform(1.0f, 2.0f, 3.0f));
    transforms.Add(child, MakeTransform(4.0f, 5.0f, 6.0f));
    hierarchy.Register(parent);
    hierarchy.SetParent(child, parent);

    CameraComponent camera{
        .Projection = ProjectionKind::Orthographic,
        .FovYRadians = 0.75f,
        .NearPlane = 0.25f,
        .FarPlane = 250.0f,
        .OrthographicHeight = 12.0f,
    };
    source.Components.Get<CameraStore>().Add(parent, camera);

    auto stream = MakeBinaryStream();
    BinaryWriter writer(stream);
    ASSERT_TRUE(SaveSceneBinary(source, writer));

    stream.seekg(0);
    BinaryReader reader(stream);
    Registry loaded;
    SceneLoadError error;
    ASSERT_TRUE(LoadSceneBinary(reader, loaded, &error)) << error.Message;

    EXPECT_EQ(loaded.Entities.Count(), 2u);

    auto& loadedTransforms = loaded.Components.Get<TransformStore<Transform3f>>();
    auto& loadedHierarchy = loaded.Resources.Get<TransformHierarchyService>();
    auto& loadedCameras = loaded.Components.Get<CameraStore>();

    ASSERT_EQ(loadedTransforms.Count(), 2u);
    ASSERT_EQ(loadedCameras.Count(), 1u);

    const auto owners = loadedTransforms.GetOwnerIds();
    EntityId loadedParent{ owners[0], 1 };
    EntityId loadedChild{ owners[1], 1 };
    if (loadedHierarchy.GetParent(loadedParent).IsValid())
        std::swap(loadedParent, loadedChild);

    ASSERT_TRUE(loadedHierarchy.GetParent(loadedChild).IsValid());
    EXPECT_EQ(loadedHierarchy.GetParent(loadedChild).Index, loadedParent.Index);
    ASSERT_NE(loadedTransforms.TryGet(loadedParent), nullptr);
    EXPECT_EQ(loadedTransforms.TryGet(loadedParent)->Local.Position, Vec3d(1.0f, 2.0f, 3.0f));

    const CameraComponent* loadedCamera = loadedCameras.TryGet(loadedParent);
    ASSERT_NE(loadedCamera, nullptr);
    EXPECT_EQ(loadedCamera->Projection, ProjectionKind::Orthographic);
    EXPECT_FLOAT_EQ(loadedCamera->OrthographicHeight, 12.0f);
}

TEST(SceneSerializer, BinaryLoadIsAdditiveAndRemapsEntityIndices)
{
    ResetSceneSerializers();
    Registry source = MakeSceneRegistry();
    EntityId sourceEntity = source.Entities.Create();
    source.Resources.Get<TransformHierarchyService>().Register(sourceEntity);
    source.Components.Get<TransformStore<Transform3f>>().Add(sourceEntity, MakeTransform(8.0f, 0.0f, 0.0f));

    auto stream = MakeBinaryStream();
    BinaryWriter writer(stream);
    ASSERT_TRUE(SaveSceneBinary(source, writer));

    Registry loaded = MakeSceneRegistry();
    EntityId preexisting = loaded.Entities.Create();
    loaded.Components.Get<TransformStore<Transform3f>>().Add(preexisting, MakeTransform(-1.0f, 0.0f, 0.0f));

    stream.seekg(0);
    BinaryReader reader(stream);
    ASSERT_TRUE(LoadSceneBinary(reader, loaded));

    EXPECT_EQ(loaded.Entities.Count(), 2u);
    auto& transforms = loaded.Components.Get<TransformStore<Transform3f>>();
    ASSERT_EQ(transforms.Count(), 2u);
    EXPECT_NE(transforms.TryGet(EntityId{ sourceEntity.Index, sourceEntity.Generation })->Local.Position,
        Vec3d(8.0f, 0.0f, 0.0f));

    bool foundRemapped = false;
    for (const auto& component : transforms.GetItems())
        foundRemapped = foundRemapped || component.Local.Position == Vec3d(8.0f, 0.0f, 0.0f);
    EXPECT_TRUE(foundRemapped);
}

TEST(SceneSerializer, JsonRoundTripsThroughStringifyAndParser)
{
    ResetSceneSerializers();
    Registry source = MakeSceneRegistry();
    EntityId entity = source.Entities.Create();
    source.Resources.Get<TransformHierarchyService>().Register(entity);
    source.Components.Get<TransformStore<Transform3f>>().Add(entity, MakeTransform(2.0f, 3.0f, 4.0f));
    source.Components.Get<CameraStore>().Add(entity, CameraComponent{});

    JsonValue json = SaveSceneJson(source);
    std::string text = JsonStringify(json, true);
    auto parsed = JsonParse(text);
    ASSERT_TRUE(parsed.has_value());

    Registry loaded;
    ASSERT_TRUE(LoadSceneJson(*parsed, loaded));

    ASSERT_EQ(loaded.Entities.Count(), 1u);
    auto& loadedTransforms = loaded.Components.Get<TransformStore<Transform3f>>();
    ASSERT_EQ(loadedTransforms.Count(), 1u);
    EXPECT_EQ(loadedTransforms.GetItems()[0].Local.Position, Vec3d(2.0f, 3.0f, 4.0f));
    EXPECT_EQ(loaded.Components.Get<CameraStore>().Count(), 1u);
}

TEST(SceneSerializer, LoadsHandAuthoredJson)
{
    ResetSceneSerializers();
    auto parsed = JsonParse(R"({
        "version": 1,
        "entities": [
            {
                "components": {
                    "Transform": {
                        "position": [0, 0, 0],
                        "rotation": [0, 0, 0, 1],
                        "scale": [1, 1, 1]
                    },
                    "Camera": {
                        "projection": "orthographic",
                        "fov_y_radians": 1.0,
                        "orthographic_height": 8.0,
                        "near_plane": 0.1,
                        "far_plane": 100.0
                    }
                }
            },
            {
                "components": {
                    "Transform": {
                        "position": [5, 0, 0],
                        "rotation": [0, 0, 0, 1],
                        "scale": [1, 1, 1]
                    }
                }
            }
        ],
        "hierarchy": [
            { "child": 0, "parent": 1 }
        ]
    })");
    ASSERT_TRUE(parsed.has_value());

    Registry loaded;
    ASSERT_TRUE(LoadSceneJson(*parsed, loaded));

    EXPECT_EQ(loaded.Entities.Count(), 2u);
    EXPECT_EQ(loaded.Components.Get<TransformStore<Transform3f>>().Count(), 2u);
    EXPECT_EQ(loaded.Components.Get<CameraStore>().Count(), 1u);
    EXPECT_EQ(loaded.Resources.Get<TransformHierarchyService>().GetRoots().size(), 1u);
}

TEST(SceneSerializer, RegistersMeshRendererThroughGenericSerializer)
{
    ResetSceneSerializers();

    bool found = false;
    for (const auto& entry : GetComponentSerializerEntries())
        found = found || entry->JsonKey() == "MeshRenderer";

    EXPECT_TRUE(found);
}

TEST(SceneSerializer, GenericComponentSerializerWritesTypedMaterialHandleAsPathString)
{
    ClearComponentSerializers();
    RegisterComponent<SceneCodecMaterialComponent>();

    LoggingProvider logging;
    AssetRegistry assetRegistry(logging);
    MaterialCache materials;
    AssetSystem assets(logging, assetRegistry, nullptr, &materials);
    MaterialHandle material = assets.RegisterProceduralMaterial(
        "asset://materials/dev/red.smat",
        Material{ .Pass = ShaderPassId::ForwardOpaque, .BaseColor = Vec4(1.0f, 0.0f, 0.0f, 1.0f) });

    Registry registry;
    EntityId entity = registry.Entities.Create();
    registry.Components.Ensure<SparseSetStore<SceneCodecMaterialComponent>>()
        .Add(entity, SceneCodecMaterialComponent{ .Material = material });

    SceneSerializationContext context{ .Assets = &assets, .Logging = &logging };
    JsonValue json = SaveSceneJson(registry, context);

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

TEST(SceneSerializer, MaterialHandleSceneCodecWritesPathString)
{
    LoggingProvider logging;
    AssetRegistry registry(logging);
    MaterialCache materials;
    AssetSystem assets(logging, registry, nullptr, &materials);
    MaterialHandle handle = assets.RegisterProceduralMaterial(
        "asset://materials/dev/red.smat",
        Material{ .Pass = ShaderPassId::ForwardOpaque, .BaseColor = Vec4(1.0f, 0.0f, 0.0f, 1.0f) });

    SceneSerializationContext context{ .Assets = &assets, .Logging = &logging };
    JsonWriteArchive archive;
    ASSERT_TRUE(SceneFieldCodec<MaterialHandle>::Save(archive, "material", handle, context));

    JsonValue json = archive.TakeValue();
    ASSERT_TRUE(json.IsString());
    EXPECT_EQ(json.AsString(), "asset://materials/dev/red.smat");
}

TEST(SceneSerializer, MaterialHandleSceneCodecLoadsPathString)
{
    LoggingProvider logging;
    AssetRegistry registry(logging);
    RegisterMaterialAsset(registry, "asset://materials/dev/red.smat");
    MaterialCache materials;
    MaterialHandle registered = materials.Register(
        "asset://materials/dev/red.smat",
        Material{ .Pass = ShaderPassId::ForwardOpaque, .BaseColor = Vec4(1.0f, 0.0f, 0.0f, 1.0f) });
    AssetSystem assets(logging, registry, nullptr, &materials);

    auto parsed = JsonParse(R"("asset://materials/dev/red.smat")");
    ASSERT_TRUE(parsed.has_value());

    SceneSerializationContext context{ .Assets = &assets, .Logging = &logging };
    JsonReadArchive archive(*parsed);
    MaterialHandle loaded;
    ASSERT_TRUE(SceneFieldCodec<MaterialHandle>::Load(archive, "", loaded, context));
    EXPECT_EQ(loaded, registered);
}

TEST(SceneSerializer, MaterialHandleSceneCodecLoadsLegacyAssetRefObject)
{
    LoggingProvider logging;
    AssetRegistry registry(logging);
    RegisterMaterialAsset(registry, "asset://materials/dev/red.smat");
    MaterialCache materials;
    MaterialHandle registered = materials.Register(
        "asset://materials/dev/red.smat",
        Material{ .Pass = ShaderPassId::ForwardOpaque, .BaseColor = Vec4(1.0f, 0.0f, 0.0f, 1.0f) });
    AssetSystem assets(logging, registry, nullptr, &materials);

    auto parsed = JsonParse(R"({ "type": "Material", "path": "asset://materials/dev/red.smat" })");
    ASSERT_TRUE(parsed.has_value());

    SceneSerializationContext context{ .Assets = &assets, .Logging = &logging };
    JsonReadArchive archive(*parsed);
    MaterialHandle loaded;
    ASSERT_TRUE(SceneFieldCodec<MaterialHandle>::Load(archive, "", loaded, context));
    EXPECT_EQ(loaded, registered);
}

TEST(SceneSerializer, MaterialHandleSceneCodecRejectsWrongTypeEmptyPathAndMissingPath)
{
    LoggingProvider logging;
    AssetRegistry registry(logging);
    MaterialCache materials;
    AssetSystem assets(logging, registry, nullptr, &materials);
    SceneSerializationContext context{ .Assets = &assets, .Logging = &logging };

    auto wrongType = JsonParse(R"({ "type": "Mesh", "path": "asset://materials/dev/red.smat" })");
    ASSERT_TRUE(wrongType.has_value());
    JsonReadArchive wrongTypeArchive(*wrongType);
    MaterialHandle loaded;
    EXPECT_FALSE(SceneFieldCodec<MaterialHandle>::Load(wrongTypeArchive, "", loaded, context));
    EXPECT_FALSE(wrongTypeArchive.Ok());

    auto emptyPath = JsonParse(R"("")");
    ASSERT_TRUE(emptyPath.has_value());
    JsonReadArchive emptyPathArchive(*emptyPath);
    EXPECT_FALSE(SceneFieldCodec<MaterialHandle>::Load(emptyPathArchive, "", loaded, context));
    EXPECT_FALSE(emptyPathArchive.Ok());

    auto missingPath = JsonParse(R"("asset://materials/dev/missing.smat")");
    ASSERT_TRUE(missingPath.has_value());
    JsonReadArchive missingPathArchive(*missingPath);
    EXPECT_FALSE(SceneFieldCodec<MaterialHandle>::Load(missingPathArchive, "", loaded, context));
    EXPECT_FALSE(missingPathArchive.Ok());
}

TEST(SceneSerializer, MaterialHandleSceneCodecRejectsRegistryTypeMismatch)
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
        Material{ .Pass = ShaderPassId::ForwardOpaque, .BaseColor = Vec4(1.0f, 0.0f, 0.0f, 1.0f) });
    AssetSystem assets(logging, registry, nullptr, &materials);

    auto parsed = JsonParse(R"("asset://materials/dev/red.smat")");
    ASSERT_TRUE(parsed.has_value());

    SceneSerializationContext context{ .Assets = &assets, .Logging = &logging };
    JsonReadArchive archive(*parsed);
    MaterialHandle loaded;
    EXPECT_FALSE(SceneFieldCodec<MaterialHandle>::Load(archive, "", loaded, context));
    EXPECT_FALSE(archive.Ok());
}

TEST(SceneSerializer, StaticMeshHandleSceneCodecRejectsWrongLegacyObjectType)
{
    auto wrongType = JsonParse(R"({ "type": "Material", "path": "asset://meshes/dev/cube.smesh" })");
    ASSERT_TRUE(wrongType.has_value());

    SceneSerializationContext context;
    JsonReadArchive archive(*wrongType);
    StaticMeshHandle loaded;
    EXPECT_FALSE(SceneFieldCodec<StaticMeshHandle>::Load(archive, "", loaded, context));
    EXPECT_FALSE(archive.Ok());
}

TEST(SceneSerializer, JsonLoadRollsBackEntitiesAndComponentsOnFailure)
{
    ResetSceneSerializers();
    auto parsed = JsonParse(R"({
        "version": 1,
        "entities": [
            {
                "components": {
                    "Transform": {
                        "position": [1, 2, 3],
                        "rotation": [0, 0, 0, 1],
                        "scale": [1, 1, 1]
                    }
                }
            }
        ],
        "hierarchy": [
            { "child": 0, "parent": 4 }
        ]
    })");
    ASSERT_TRUE(parsed.has_value());

    Registry loaded = MakeSceneRegistry();
    SceneLoadError error;
    EXPECT_FALSE(LoadSceneJson(*parsed, loaded, &error));

    EXPECT_EQ(loaded.Entities.Count(), 0u);
    EXPECT_EQ(loaded.Components.Get<TransformStore<Transform3f>>().Count(), 0u);
    EXPECT_EQ(loaded.Resources.Get<TransformHierarchyService>().Count(), 0u);
}

TEST(SceneSerializer, BinaryLoadRollsBackCreatedEntitiesOnFailure)
{
    ResetSceneSerializers();
    auto stream = MakeBinaryStream();
    BinaryWriter writer(stream);

    ASSERT_TRUE(WriteBinaryHeader(writer, SceneMagic, SceneVersion));

    {
        ChunkWriter chunk;
        ASSERT_TRUE(chunk.Begin(writer, SceneChunk::Registry, SceneVersion));
        ASSERT_TRUE(Serialize(writer, std::uint32_t{ 1 }));
        ASSERT_TRUE(Serialize(writer, EntityIndex{ 0 }));
        ASSERT_TRUE(Serialize(writer, std::uint16_t{ 1 }));
        ASSERT_TRUE(chunk.End(writer));
    }

    {
        ChunkWriter chunk;
        ASSERT_TRUE(chunk.Begin(writer, SceneChunk::Cameras, SceneVersion));
        ASSERT_TRUE(Serialize(writer, std::uint32_t{ 1 }));
        ASSERT_TRUE(Serialize(writer, EntityIndex{ 99 }));
        ASSERT_TRUE(chunk.End(writer));
    }

    stream.seekg(0);
    BinaryReader reader(stream);
    Registry loaded = MakeSceneRegistry();
    SceneLoadError error;
    EXPECT_FALSE(LoadSceneBinary(reader, loaded, &error));

    EXPECT_EQ(loaded.Entities.Count(), 0u);
    EXPECT_EQ(loaded.Components.Get<CameraStore>().Count(), 0u);
}

TEST(SceneSerializer, BinarySkipsUnknownChunks)
{
    ResetSceneSerializers();
    auto stream = MakeBinaryStream();
    BinaryWriter writer(stream);

    ASSERT_TRUE(WriteBinaryHeader(writer, SceneMagic, SceneVersion));

    {
        ChunkWriter chunk;
        ASSERT_TRUE(chunk.Begin(writer, SceneChunk::Registry, SceneVersion));
        ASSERT_TRUE(Serialize(writer, std::uint32_t{ 1 }));
        ASSERT_TRUE(Serialize(writer, EntityIndex{ 0 }));
        ASSERT_TRUE(Serialize(writer, std::uint16_t{ 1 }));
        ASSERT_TRUE(chunk.End(writer));
    }

    {
        ChunkWriter chunk;
        ASSERT_TRUE(chunk.Begin(writer, MakeFourCC('T', 'E', 'S', 'T'), SceneVersion));
        ASSERT_TRUE(Serialize(writer, std::uint32_t{ 0xDEADBEEFu }));
        ASSERT_TRUE(chunk.End(writer));
    }

    {
        ChunkWriter chunk;
        ASSERT_TRUE(chunk.Begin(writer, SceneChunk::Cameras, SceneVersion));
        ASSERT_TRUE(Serialize(writer, std::uint32_t{ 1 }));
        ASSERT_TRUE(Serialize(writer, EntityIndex{ 0 }));
        ASSERT_TRUE(Serialize(writer, CameraComponent{}));
        ASSERT_TRUE(chunk.End(writer));
    }

    stream.seekg(0);
    BinaryReader reader(stream);
    Registry loaded;
    ASSERT_TRUE(LoadSceneBinary(reader, loaded));

    EXPECT_EQ(loaded.Entities.Count(), 1u);
    EXPECT_EQ(loaded.Components.Get<CameraStore>().Count(), 1u);
}

TEST(SceneSerializer, HandlesEmptyRegistry)
{
    ResetSceneSerializers();
    Registry source;
    auto stream = MakeBinaryStream();
    BinaryWriter writer(stream);
    ASSERT_TRUE(SaveSceneBinary(source, writer));

    stream.seekg(0);
    BinaryReader reader(stream);
    Registry loaded;
    ASSERT_TRUE(LoadSceneBinary(reader, loaded));
    EXPECT_EQ(loaded.Entities.Count(), 0u);

    JsonValue json = SaveSceneJson(source);
    Registry jsonLoaded;
    ASSERT_TRUE(LoadSceneJson(json, jsonLoaded));
    EXPECT_EQ(jsonLoaded.Entities.Count(), 0u);
}
