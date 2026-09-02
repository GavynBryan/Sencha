// An asset kind the engine has never heard of, brought by a module: a stager,
// a cache, and one registration on the front door. Everything downstream --
// the load, the component that owns the reference, the scene that persists
// it, the editor field that edits it -- has to work without a central edit.
// That is what makes a module-defined kind cost what a built-in one costs.

#include "document/AssetFieldIo.h"

#include <assets/runtime/AssetSystem.h>
#include <core/assets/AssetCache.h>
#include <core/assets/AssetKindRegistry.h>
#include <core/assets/AssetLease.h>
#include <core/assets/AssetRef.h>
#include <core/assets/AssetRegistry.h>
#include <core/assets/AssetSource.h>
#include <core/assets/AssetStager.h>
#include <core/assets/AssetStoreTable.h>
#include <core/handle/Handle.h>
#include <core/json/JsonParser.h>
#include <core/json/JsonValue.h>
#include <core/logging/LoggingProvider.h>
#include <core/metadata/Field.h>
#include <core/metadata/TypeSchema.h>
#include <core/serialization/FourCC.h>
#include <ecs/ComponentTraits.h>
#include <ecs/ComponentTypeId.h>
#include <ecs/World.h>
#include <world/ComponentAssetOwnership.h>
#include <world/ComponentRegistrar.h>
#include <world/registry/Registry.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/SceneSerializationContext.h>
#include <world/serialization/SceneSerializer.h>

#include <gtest/gtest.h>

#include <any>
#include <cstdint>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

namespace
{
    using ScriptHandle = Handle<struct ScriptHandleTag>;

    struct ScriptEntry
    {
        std::string Source;
        std::uint32_t Generation = 0;
        std::uint32_t RefCount = 0;
        std::string PathKey;
        bool Alive = false;
    };

    // The whole of a module's cache: the generic pool does residency and
    // reference counting, the module says what an entry holds.
    class ScriptCache final
        : public AssetCache<ScriptCache, ScriptHandle, ScriptEntry, AssetType::Script>
    {
    public:
        ScriptCache() { ReserveNullSlot(); }
        ~ScriptCache() override { FreeAllEntries(); }

        ScriptHandle Insert(std::string_view path, std::string source)
        {
            ScriptEntry entry;
            entry.Source = std::move(source);
            entry.Alive = true;
            return AllocNamedHandle(path, std::move(entry));
        }

        std::uint32_t ReferencesTo(std::string_view path) const
        {
            const ScriptEntry* entry = Resolve(FindRegisteredHandle(path));
            return entry ? entry->RefCount : 0;
        }

    private:
        friend class AssetCache<ScriptCache, ScriptHandle, ScriptEntry, AssetType::Script>;

        void OnFree(ScriptEntry& entry) { entry.Alive = false; }
        bool IsEntryLive(const ScriptEntry& entry) const { return entry.Alive; }
    };

    // Decoding is not under test; the record's own path stands in for the
    // bytes a real stager would read.
    class ScriptStager final : public IAssetStager
    {
    public:
        AssetStaging LoadStaged(const AssetRecord& record, IAssetSource&) override
        {
            AssetStaging staged;
            staged.Record = record;
            staged.Payload = std::string(record.Path);
            return staged;
        }
    };

    struct ScriptBinding
    {
        ScriptHandle Script;
    };

    constexpr std::string_view kPatrol = "asset://scripts/patrol.sscript";
    constexpr std::string_view kIdle = "asset://scripts/idle.sscript";

    AssetFieldValue Value(std::string_view path)
    {
        AssetFieldValue value;
        value.Refs.push_back(AssetFieldRef{ {}, std::string(path) });
        return value;
    }
}

template <>
struct TypeSchema<ScriptBinding>
{
    static constexpr std::string_view Name = "test.script_binding";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('T', 'S', 'C', 'R');

    static auto Fields()
    {
        return std::tuple{
            MakeField("script", &ScriptBinding::Script).AsAsset(AssetType::Script),
        };
    }
};

SENCHA_DECLARE_COMPONENT_TYPE(ScriptBinding, "test.script_binding");

template <>
struct ComponentTraits<ScriptBinding> : SchemaAssetOwnership<ScriptBinding>
{
};

namespace
{
    class AssetKindExtensibility : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            AssetKindRegistration kind;
            kind.Type = AssetType::Script;
            kind.Name = "Script";
            kind.RuntimeExtensions = { ".sscript" };
            kind.Stager = &Stager;
            kind.Store = &Scripts;
            kind.Commit = [this](AssetStaging&& staged)
            {
                const ScriptHandle handle = Scripts.Insert(
                    staged.Record.Path, std::any_cast<std::string>(std::move(staged.Payload)));
                return AssetLease::Adopt(AssetType::Script, Scripts, handle.ToToken());
            };
            ASSERT_TRUE(Assets.Kinds().Register(std::move(kind)));

            for (std::string_view path : { kPatrol, kIdle })
            {
                AssetRecord record;
                record.Type = AssetType::Script;
                record.SourceKind = AssetSourceKind::Procedural;
                record.Path = std::string(path);
                ASSERT_TRUE(Registry_.Register(record));
            }

            ComponentRegistrar registrar(Scene.Components);
            registrar.Add<ScriptBinding>();
            RegisterComponent<ScriptBinding>(Serializers);
            Scene.Components.SetResource(Assets.Stores());
        }

        EntityId TheOnlyEntity()
        {
            EntityId found{};
            Scene.Components.ForEachComponent<ScriptBinding>(
                [&](EntityId entity, const ScriptBinding&) { found = entity; });
            return found;
        }

        LoggingProvider Logging;
        AssetRegistry Registry_{ Logging };
        ScriptStager Stager;
        ScriptCache Scripts;
        AssetSystem Assets{ Logging, Registry_, nullptr, nullptr };
        ComponentSerializerRegistry Serializers;
        Registry Scene;
    };
}

TEST_F(AssetKindExtensibility, TheFrontDoorLoadsAKindItWasBuiltWithout)
{
    EXPECT_TRUE(Assets.HasStore(AssetType::Script));
    EXPECT_EQ(Assets.Stores().Find(AssetType::Script, AssetArity::Single), &Scripts);

    {
        const AssetLease lease = Assets.LoadLease(kPatrol, AssetType::Script);
        ASSERT_TRUE(lease.IsValid());
        EXPECT_EQ(Scripts.ReferencesTo(kPatrol), 1u);
        EXPECT_EQ(Assets.GetPathForLease(AssetType::Script, lease.OpaqueToken()), kPatrol);

        const AssetLease again = Assets.LoadLease(kPatrol, AssetType::Script);
        EXPECT_EQ(Scripts.ReferencesTo(kPatrol), 2u) << "a resident path is shared, not re-staged";
    }
    EXPECT_FALSE(Scripts.IsResident(kPatrol));
}

TEST_F(AssetKindExtensibility, ASceneLoadedComponentOwnsTheReference)
{
    const auto parsed = JsonParse(R"({
        "version": 1,
        "entities": [
            { "components": { "test.script_binding": { "script": "asset://scripts/patrol.sscript" } } }
        ]
    })");
    ASSERT_TRUE(parsed.has_value());

    SceneSerializationContext context(Logging, &Assets);
    ASSERT_TRUE(LoadSceneJson(*parsed, Scene, Serializers, context));

    EXPECT_EQ(Scripts.ReferencesTo(kPatrol), 1u)
        << "the load's own reference was handed to the component, not kept beside it";

    Scene.Components.DestroyEntity(TheOnlyEntity());
    EXPECT_FALSE(Scripts.IsResident(kPatrol));
}

TEST_F(AssetKindExtensibility, ASavedSceneNamesTheScriptBack)
{
    const EntityId entity = Scene.Components.CreateEntity();
    {
        AssetLease loaded = Assets.LoadLease(kPatrol, AssetType::Script);
        ASSERT_TRUE(loaded.IsValid());
        Scene.Components.AddComponent(entity, ScriptBinding{ ScriptHandle::FromToken(loaded.OpaqueToken()) });
    }
    EXPECT_EQ(Scripts.ReferencesTo(kPatrol), 1u);

    SceneSerializationContext context(Logging, &Assets);
    const JsonValue saved = SaveSceneJson(Scene, Serializers, context);

    const JsonValue* binding = saved.Find("entities")->AsArray().at(0)
        .Find("components")->Find("test.script_binding");
    ASSERT_NE(binding, nullptr);
    EXPECT_EQ(binding->Find("script")->AsString(), kPatrol);
}

TEST_F(AssetKindExtensibility, TheEditorEditsTheFieldLikeAnyOther)
{
    ScriptHandle field{};

    ApplyAssetField(Assets, AssetType::Script, AssetArity::Single, &field, Value(kPatrol));
    EXPECT_EQ(Scripts.ReferencesTo(kPatrol), 1u);
    EXPECT_EQ(ReadAssetField(Assets, AssetType::Script, AssetArity::Single, &field).Refs.at(0).Path,
              kPatrol);

    ApplyAssetField(Assets, AssetType::Script, AssetArity::Single, &field, Value(kIdle));
    EXPECT_FALSE(Scripts.IsResident(kPatrol));
    EXPECT_EQ(Scripts.ReferencesTo(kIdle), 1u);

    ApplyAssetField(Assets, AssetType::Script, AssetArity::Single, &field, AssetFieldValue{});
    EXPECT_FALSE(field.IsValid());
    EXPECT_FALSE(Scripts.IsResident(kIdle));
}
