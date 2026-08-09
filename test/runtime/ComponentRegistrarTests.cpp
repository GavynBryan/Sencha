#include <gtest/gtest.h>

#include <core/metadata/Field.h>
#include <core/serialization/FourCC.h>
#include <world/ComponentRegistrar.h>
#include <world/RuntimeComponentSchema.h>
#include <world/registry/Registry.h>
#include <world/registry/SceneRegistryInitialization.h>
#include <world/serialization/SceneSerializer.h>

#include <algorithm>
#include <cstdint>
#include <string_view>
#include <vector>

//=============================================================================
// What a component declares, and what follows from it.
//
// These used to be assertions about the contents of a list. There is no list
// now -- a component's TypeSchema says whether it persists and whether it
// travels, and one Add reads that -- so what is left to protect is that the
// reading is right and that the registries it fills stay consistent with each
// other.
//=============================================================================

namespace
{
    struct StorageOnlyComponent
    {
        int Value = 0;
    };

    struct SavedComponent
    {
        float Value = 2.5f;
    };

    struct SentComponent
    {
        float Value = 0.0f;
    };

    struct MismatchedDefaultComponent
    {
        float Value = 1.0f;
    };
}

SENCHA_DECLARE_COMPONENT_TYPE(StorageOnlyComponent, "test.storage_only");

template <>
struct TypeSchema<SavedComponent>
{
    static constexpr std::string_view Name = "test.saved";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('T', 'S', 'A', 'V');

    static auto Fields()
    {
        const SavedComponent defaults;
        return std::tuple{
            MakeField("value", &SavedComponent::Value).Default(defaults.Value),
        };
    }
};

template <>
struct TypeSchema<SentComponent>
{
    static constexpr std::string_view Name = "test.sent";
    static constexpr bool Replicated = true;

    static auto Fields()
    {
        return std::tuple{
            MakeField("value", &SentComponent::Value),
        };
    }
};

template <>
struct TypeSchema<MismatchedDefaultComponent>
{
    static constexpr std::string_view Name = "test.mismatched_default";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('T', 'M', 'I', 'S');

    static auto Fields()
    {
        // Deliberately not the member initializer above.
        return std::tuple{
            MakeField("value", &MismatchedDefaultComponent::Value).Default(9.0f),
        };
    }
};

TEST(ComponentRegistrar, ASchemaDecidesWhichRegistriesAComponentReaches)
{
    WorldComponentSchema schema;
    ComponentSerializerRegistry serializers;
    ReplicationLayout layout;

    ComponentRegistrar registrar(&schema, &serializers, &layout);
    registrar.Add<StorageOnlyComponent>();
    registrar.Add<SavedComponent>();
    registrar.Add<SentComponent>();

    // Storage is unconditional: a component exists because something named it.
    EXPECT_TRUE(schema.Contains(ResolveComponentTypeId<StorageOnlyComponent>()));
    EXPECT_TRUE(schema.Contains(ResolveComponentTypeId<SavedComponent>()));
    EXPECT_TRUE(schema.Contains(ResolveComponentTypeId<SentComponent>()));

    // A chunk id and nothing else means saved and not sent.
    EXPECT_NE(serializers.FindByType(ResolveComponentTypeId<SavedComponent>()), nullptr);
    EXPECT_EQ(layout.Find(ResolveComponentTypeId<SavedComponent>()), nullptr);

    // Replicated and nothing else means sent and not saved.
    EXPECT_NE(layout.Find(ResolveComponentTypeId<SentComponent>()), nullptr);
    EXPECT_EQ(serializers.FindByType(ResolveComponentTypeId<SentComponent>()), nullptr);

    // Neither means storage alone.
    EXPECT_EQ(serializers.FindByType(ResolveComponentTypeId<StorageOnlyComponent>()), nullptr);
    EXPECT_EQ(layout.Find(ResolveComponentTypeId<StorageOnlyComponent>()), nullptr);
}

// A host takes the registries it owns and no others. The editor edits scenes
// with no World and no session; nothing about that should make it a special
// case in any feature's registrar.
TEST(ComponentRegistrar, AHostWithOneRegistryFillsOnlyThatOne)
{
    ComponentSerializerRegistry serializers;
    ComponentRegistrar registrar(nullptr, &serializers, nullptr);
    registrar.Add<SavedComponent>();
    registrar.Add<SentComponent>();

    EXPECT_NE(serializers.FindByType(ResolveComponentTypeId<SavedComponent>()), nullptr);
    // The replicated one was named, had nowhere to go, and did not fail.
    EXPECT_EQ(serializers.FindByType(ResolveComponentTypeId<SentComponent>()), nullptr);
}

TEST(ComponentRegistrar, RegisteringTheSameComponentTwiceChangesNothing)
{
    WorldComponentSchema schema;
    ComponentSerializerRegistry serializers;
    ReplicationLayout layout;

    ComponentRegistrar registrar(&schema, &serializers, &layout);
    registrar.Add<SavedComponent>();
    registrar.Add<SentComponent>();

    const std::size_t storage = schema.Size();
    const std::size_t saved = serializers.Entries().size();
    const std::size_t sent = layout.Size();

    registrar.Add<SavedComponent>();
    registrar.Add<SentComponent>();

    EXPECT_EQ(schema.Size(), storage);
    EXPECT_EQ(serializers.Entries().size(), saved);
    // Wire keys are positions, so a duplicate would silently shift every
    // component after it.
    EXPECT_EQ(layout.Size(), sent);
}

TEST(ComponentRegistrar, ADeclaredSerializerIsRecordedForTheHostToRetract)
{
    ComponentSerializerRegistry serializers;
    ComponentRegistrar registrar(nullptr, &serializers, nullptr);
    registrar.Add<SavedComponent>();
    registrar.Add<SentComponent>();

    // Only what actually entered the registry, so a host retracting the list
    // cannot remove a serializer some other host owns.
    ASSERT_EQ(registrar.AddedSerializers().size(), 1u);
    EXPECT_EQ(registrar.AddedSerializers()[0], ResolveComponentTypeId<SavedComponent>());

    for (ComponentTypeId type : registrar.AddedSerializers())
        EXPECT_TRUE(serializers.Remove(type));
    EXPECT_EQ(serializers.Entries().size(), 0u);
}

// The rule that lets a writer omit a field equal to its default and a reader
// leave an absent one alone. Asserted at registration for every component;
// tested here on the predicate, because an assertion cannot be observed.
TEST(ComponentRegistrar, ASchemaDefaultMustEqualItsMemberInitializer)
{
    EXPECT_TRUE(ComponentDefaultsMatchInitializers<SavedComponent>());
    EXPECT_FALSE(ComponentDefaultsMatchInitializers<MismatchedDefaultComponent>());
}

//=============================================================================
// The engine's own composition
//=============================================================================

TEST(EngineComponents, ChunkIdsAndSchemaNamesAreUnique)
{
    ComponentSerializerRegistry serializers;
    ComponentRegistrar registrar(nullptr, &serializers, nullptr);
    RegisterEngineComponents(registrar);

    std::vector<std::uint32_t> chunkIds;
    std::vector<std::string_view> names;
    for (const auto& entry : serializers.Entries())
    {
        chunkIds.push_back(entry->BinaryChunkId());
        names.push_back(entry->JsonKey());
    }

    std::ranges::sort(chunkIds);
    EXPECT_EQ(std::ranges::adjacent_find(chunkIds), chunkIds.end())
        << "Two engine components share a SceneChunkId";

    std::ranges::sort(names);
    EXPECT_EQ(std::ranges::adjacent_find(names), names.end())
        << "Two engine components share a TypeSchema::Name";
}

// The editor's preview registry composes from the same registrars the runtime
// does, so it cannot know a different set of components than the game does.
TEST(EngineComponents, TheSceneRegistryGetsTheSameVocabularyAsTheRuntime)
{
    Registry registry;
    InitializeSceneRegistry(registry);

    ComponentSerializerRegistry serializers;
    ComponentRegistrar registrar(nullptr, &serializers, nullptr);
    RegisterEngineComponents(registrar);

    for (const auto& entry : serializers.Entries())
    {
        EXPECT_NE(registry.Components.GetComponentIdByType(entry->TypeId()),
                  InvalidComponentId)
            << entry->JsonKey() << " missing from the scene registry";
    }

    // The derived columns come with the transform rather than as chunks of
    // their own, and a scene cannot be propagated without them.
    EXPECT_TRUE(registry.Components.IsRegistered<WorldTransform>());
    EXPECT_TRUE(registry.Components.IsRegistered<Parent>());
}

TEST(EngineComponents, SerializerRegistrationIsIdempotent)
{
    ComponentSerializerRegistry serializers;
    RegisterEngineSceneSerializers(serializers);
    const std::size_t count = serializers.Entries().size();
    EXPECT_GT(count, 0u);

    RegisterEngineSceneSerializers(serializers);
    EXPECT_EQ(serializers.Entries().size(), count)
        << "Re-registering the engine's components must not duplicate entries";
}
