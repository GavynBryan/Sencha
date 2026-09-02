// A component whose schema names asset fields owns one reference per field,
// taken through the World's store table when the component arrives and given
// back when it leaves. What is protected here is the mapping from schema to
// store calls -- every asset leaf, in order, to the store for its kind and
// arity -- rather than any particular cache.

#include <gtest/gtest.h>

#include <core/assets/AssetLease.h>
#include <core/assets/AssetRef.h>
#include <core/assets/AssetStore.h>
#include <core/assets/AssetStoreTable.h>
#include <core/handle/Handle.h>
#include <core/metadata/Field.h>
#include <core/metadata/TypeSchema.h>
#include <ecs/ComponentTraits.h>
#include <ecs/ComponentTypeId.h>
#include <ecs/World.h>
#include <world/ComponentAssetOwnership.h>

#include <cstdint>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace
{
    struct Surface
    {
        Handle<struct SurfaceTextureTag> Albedo;
        float Tint = 1.0f;
    };

    struct Prop
    {
        Handle<struct PropMeshTag> Mesh;
        Handle<struct PropMaterialsTag> Materials;
        Surface Skin;
        int Layer = 0;
    };

    struct StoreCall
    {
        AssetType Kind;
        AssetArity Arity;
        bool Retained;
        std::uint64_t Token;

        bool operator==(const StoreCall&) const = default;
    };

    // Every store in a test appends to one log, so the order across stores is
    // what gets asserted, not a count per store.
    class RecordingStore final : public IAssetStore
    {
    public:
        RecordingStore(AssetType kind, AssetArity arity, std::vector<StoreCall>& log)
            : Kind(kind)
            , Arity(arity)
            , Log(log)
        {
        }

        AssetType Type() const override { return Kind; }
        bool IsResident(std::string_view) const override { return false; }
        AssetLease TryAcquireLease(std::string_view) override { return {}; }
        std::string_view GetPath(std::uint64_t) const override { return {}; }

        void RetainToken(std::uint64_t token) override
        {
            Log.push_back({ Kind, Arity, true, token });
        }
        void ReleaseToken(std::uint64_t token) override
        {
            Log.push_back({ Kind, Arity, false, token });
        }

    private:
        AssetType Kind;
        AssetArity Arity;
        std::vector<StoreCall>& Log;
    };

    Prop MakeProp()
    {
        Prop prop;
        prop.Mesh = { 1, 1 };
        prop.Materials = { 2, 1 };
        prop.Skin.Albedo = { 3, 1 };
        return prop;
    }

    StoreCall Retain(AssetType kind, AssetArity arity, std::uint64_t token)
    {
        return { kind, arity, true, token };
    }

    StoreCall Release(AssetType kind, AssetArity arity, std::uint64_t token)
    {
        return { kind, arity, false, token };
    }
}

template <>
struct TypeSchema<Surface>
{
    static constexpr std::string_view Name = "test.surface";

    static auto Fields()
    {
        return std::tuple{
            MakeField("albedo", &Surface::Albedo).AsAsset(AssetType::Texture),
            MakeField("tint", &Surface::Tint),
        };
    }
};

template <>
struct TypeSchema<Prop>
{
    static constexpr std::string_view Name = "test.prop";

    static auto Fields()
    {
        return std::tuple{
            MakeField("mesh", &Prop::Mesh).AsAsset(AssetType::StaticMesh),
            MakeField("materials", &Prop::Materials)
                .AsAsset(AssetType::Material, AssetArity::List),
            MakeField("skin", &Prop::Skin),
            MakeField("layer", &Prop::Layer),
        };
    }
};

SENCHA_DECLARE_COMPONENT_TYPE(Prop, "test.prop");

template <>
struct ComponentTraits<Prop> : SchemaAssetOwnership<Prop>
{
};

TEST(SchemaAssetOwnership, AssetFieldsAreTheSchemasAssetLeavesInOrder)
{
    std::vector<std::string_view> names;
    for (const RuntimeField& field : AssetFieldsOf<Prop>())
        names.push_back(field.Name);

    EXPECT_EQ(names, (std::vector<std::string_view>{ "mesh", "materials", "skin.albedo" }));
}

TEST(SchemaAssetOwnership, RetainsInSchemaOrderAndReleasesInReverse)
{
    std::vector<StoreCall> log;
    RecordingStore meshes(AssetType::StaticMesh, AssetArity::Single, log);
    RecordingStore materialSets(AssetType::Material, AssetArity::List, log);
    RecordingStore textures(AssetType::Texture, AssetArity::Single, log);

    AssetStoreTable stores;
    stores.Add(AssetType::StaticMesh, AssetArity::Single, meshes);
    stores.Add(AssetType::Material, AssetArity::List, materialSets);
    stores.Add(AssetType::Texture, AssetArity::Single, textures);

    World world;
    world.RegisterComponent<Prop>();
    world.AddResource<AssetStoreTable>(std::move(stores));

    const Prop prop = MakeProp();
    const EntityId entity = world.CreateEntity();
    world.AddComponent(entity, prop);
    EXPECT_EQ(log, (std::vector<StoreCall>{
        Retain(AssetType::StaticMesh, AssetArity::Single, prop.Mesh.ToToken()),
        Retain(AssetType::Material, AssetArity::List, prop.Materials.ToToken()),
        Retain(AssetType::Texture, AssetArity::Single, prop.Skin.Albedo.ToToken()),
    }));

    log.clear();
    world.DestroyEntity(entity);
    EXPECT_EQ(log, (std::vector<StoreCall>{
        Release(AssetType::Texture, AssetArity::Single, prop.Skin.Albedo.ToToken()),
        Release(AssetType::Material, AssetArity::List, prop.Materials.ToToken()),
        Release(AssetType::StaticMesh, AssetArity::Single, prop.Mesh.ToToken()),
    }));
}

// A list of one kind is held by a different store than a single of the same
// kind, and the field's arity picks between them.
TEST(SchemaAssetOwnership, AListFieldReachesTheKindsListStore)
{
    std::vector<StoreCall> log;
    RecordingStore materials(AssetType::Material, AssetArity::Single, log);
    RecordingStore materialSets(AssetType::Material, AssetArity::List, log);

    AssetStoreTable stores;
    stores.Add(AssetType::Material, AssetArity::Single, materials);
    stores.Add(AssetType::Material, AssetArity::List, materialSets);

    World world;
    world.RegisterComponent<Prop>();
    world.AddResource<AssetStoreTable>(std::move(stores));

    const Prop prop = MakeProp();
    world.AddComponent(world.CreateEntity(), prop);
    EXPECT_EQ(log, (std::vector<StoreCall>{
        Retain(AssetType::Material, AssetArity::List, prop.Materials.ToToken()),
    }));
}

// A table that has no store for a field's kind holds nothing for that field
// and everything for the rest.
TEST(SchemaAssetOwnership, AFieldWithoutAStoreIsSkipped)
{
    std::vector<StoreCall> log;
    RecordingStore textures(AssetType::Texture, AssetArity::Single, log);

    AssetStoreTable stores;
    stores.Add(AssetType::Texture, AssetArity::Single, textures);

    World world;
    world.RegisterComponent<Prop>();
    world.AddResource<AssetStoreTable>(std::move(stores));

    const Prop prop = MakeProp();
    const EntityId entity = world.CreateEntity();
    world.AddComponent(entity, prop);
    world.DestroyEntity(entity);
    EXPECT_EQ(log, (std::vector<StoreCall>{
        Retain(AssetType::Texture, AssetArity::Single, prop.Skin.Albedo.ToToken()),
        Release(AssetType::Texture, AssetArity::Single, prop.Skin.Albedo.ToToken()),
    }));
}

// A World that was never given stores carries the handles as plain values.
TEST(SchemaAssetOwnership, AWorldWithoutStoresHoldsNothing)
{
    World world;
    world.RegisterComponent<Prop>();

    const EntityId entity = world.CreateEntity();
    world.AddComponent(entity, MakeProp());
    EXPECT_EQ(world.TryGet<Prop>(entity)->Mesh, MakeProp().Mesh);
    world.DestroyEntity(entity);
}

// Replacing the table is how a host takes the stores back before the caches
// go away: a component removed afterwards finds nothing to release into.
TEST(SchemaAssetOwnership, AnEmptiedTableReleasesNothing)
{
    std::vector<StoreCall> log;
    RecordingStore meshes(AssetType::StaticMesh, AssetArity::Single, log);

    AssetStoreTable stores;
    stores.Add(AssetType::StaticMesh, AssetArity::Single, meshes);

    World world;
    world.RegisterComponent<Prop>();
    world.AddResource<AssetStoreTable>(std::move(stores));

    const EntityId entity = world.CreateEntity();
    world.AddComponent(entity, MakeProp());
    ASSERT_EQ(log.size(), 1u);

    world.SetResource(AssetStoreTable{});
    world.DestroyEntity(entity);
    EXPECT_EQ(log.size(), 1u);
}
