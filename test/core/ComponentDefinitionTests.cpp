// The generated-descriptor mechanism, exercised on hand-written descriptors.
//
// The generator's output is just a ComponentDefinition specialization, so
// everything downstream of it can be tested without running the generator: what
// is proved here is that a definition projects into the traits its consumers
// already read, that the optional facts really are optional, and that a
// handwritten specialization still outranks the projection -- which is what
// lets components migrate one at a time.

#include <core/metadata/ComponentDefinition.h>
#include <core/metadata/ComponentRemovable.h>
#include <core/metadata/EditorVisual.h>
#include <core/metadata/Field.h>
#include <core/metadata/RuntimeSchema.h>
#include <core/metadata/TypeSchema.h>
#include <core/serialization/FourCC.h>
#include <ecs/ComponentTypeId.h>
#include <world/ComponentSet.h>

#include <gtest/gtest.h>

#include <string_view>
#include <tuple>

// A requires-expression needs a template parameter to be a substitution
// context; asking about a concrete type directly is a hard error, not a false.
template <typename T> inline constexpr bool SchemaHasChunk = requires { TypeSchema<T>::SceneChunkId; };
template <typename T> inline constexpr bool SchemaHasReplicated = requires { TypeSchema<T>::Replicated; };
template <typename T> inline constexpr bool SchemaHasPredicted = requires { TypeSchema<T>::Predicted; };

// ─── A fully-declared component ──────────────────────────────────────────────

struct DefinedFull
{
    float X = 1.5f;
    int   Y = 7;
};

template <>
struct ComponentDefinition<DefinedFull>
{
    static constexpr std::string_view Identity   = "test.defined_full";
    static constexpr std::string_view SchemaName = "DefinedFull";
    static constexpr std::uint32_t    SceneChunk = MakeFourCC('D', 'F', 'U', 'L');
    static constexpr bool Replicated = true;
    static constexpr bool Predicted  = true;
    static constexpr bool Removable  = false;
    static constexpr std::string_view VisualMeshAsset = "camera.glb";

    static auto Fields()
    {
        const DefinedFull defaults;
        return std::tuple{
            MakeField("x", &DefinedFull::X).Default(defaults.X).Label("Ex"),
            MakeField("y", &DefinedFull::Y).Default(defaults.Y),
        };
    }
};

// ─── Identity only: a pure-runtime component ─────────────────────────────────

struct DefinedIdentityOnly
{
    int Value = 0;
};

template <>
struct ComponentDefinition<DefinedIdentityOnly>
{
    static constexpr std::string_view Identity = "test.defined_identity_only";
};

// ─── A schema with no scene chunk, the MovementTuningSource shape ────────────

struct DefinedNoChunk
{
    int Value = 0;
};

template <>
struct ComponentDefinition<DefinedNoChunk>
{
    static constexpr std::string_view Identity   = "test.defined_no_chunk";
    static constexpr std::string_view SchemaName = "DefinedNoChunk";

    static auto Fields() { return std::tuple{ MakeField("value", &DefinedNoChunk::Value) }; }
};

// ─── A component that has a definition AND a handwritten schema ──────────────

struct DefinedButOverridden
{
    int Value = 0;
};

template <>
struct ComponentDefinition<DefinedButOverridden>
{
    static constexpr std::string_view Identity   = "test.defined_overridden";
    static constexpr std::string_view SchemaName = "FromDefinition";

    static auto Fields() { return std::tuple{ MakeField("generated", &DefinedButOverridden::Value) }; }
};

// The handwritten one, which must win.
template <>
struct TypeSchema<DefinedButOverridden>
{
    static constexpr std::string_view Name = "FromHand";

    static auto Fields() { return std::tuple{ MakeField("handwritten", &DefinedButOverridden::Value) }; }
};

// ─── Identity ────────────────────────────────────────────────────────────────

TEST(ComponentDefinitionProjection, IdentityResolvesFromTheDefinition)
{
    EXPECT_EQ(ResolveComponentTypeId<DefinedFull>(),
              MakeComponentTypeId("test.defined_full"));
    EXPECT_EQ(ResolveComponentName<DefinedFull>(), "test.defined_full");
}

// The reason descriptors are per-header companions: a component with nothing
// but an identity is still nameable, which is what every typed structural call
// needs from an arbitrary translation unit.
TEST(ComponentDefinitionProjection, IdentityAloneIsEnoughToNameAComponent)
{
    EXPECT_EQ(ResolveComponentTypeId<DefinedIdentityOnly>(),
              MakeComponentTypeId("test.defined_identity_only"));
    static_assert(!HasTypeSchema<DefinedIdentityOnly>,
                  "a definition without SchemaName must not conjure a schema");
}

// ─── Schema ──────────────────────────────────────────────────────────────────

TEST(ComponentDefinitionProjection, SchemaProjectsNameFieldsAndPolicy)
{
    static_assert(HasTypeSchema<DefinedFull>);
    EXPECT_EQ(TypeSchema<DefinedFull>::Name, "DefinedFull");
    EXPECT_EQ(TypeSchema<DefinedFull>::SceneChunkId, MakeFourCC('D', 'F', 'U', 'L'));
    EXPECT_TRUE(TypeSchema<DefinedFull>::Replicated);
    EXPECT_TRUE(TypeSchema<DefinedFull>::Predicted);

    // The fields arrive whole, annotations included.
    const auto& fields = RuntimeFieldsOf<DefinedFull>();
    ASSERT_EQ(fields.size(), 2u);
    EXPECT_EQ(fields[0].Name, "x");
    EXPECT_EQ(fields[0].Label, "Ex");
    EXPECT_EQ(fields[1].Name, "y");
}

// Whether a component is scene-serialized is decided by asking whether the
// member exists, so a definition without a chunk must not grow one.
TEST(ComponentDefinitionProjection, AnAbsentFactIsAbsentNotDefaulted)
{
    static_assert(HasTypeSchema<DefinedNoChunk>);
    static_assert(!SchemaHasChunk<DefinedNoChunk>,
                  "a component with no scene chunk must not appear serialized");
    static_assert(!SchemaHasReplicated<DefinedNoChunk>,
                  "a component that does not travel must not appear replicated");
    static_assert(!SchemaHasPredicted<DefinedNoChunk>);
    static_assert(SchemaHasChunk<DefinedFull>, "and present when it is declared");
}

// ─── Editor policy ───────────────────────────────────────────────────────────

TEST(ComponentDefinitionProjection, EditorPolicyProjects)
{
    EXPECT_FALSE(ComponentRemovable<DefinedFull>::Value);
    EXPECT_TRUE(ComponentRemovable<DefinedNoChunk>::Value) << "removable by default";

    ASSERT_TRUE(ComponentEditorVisual<DefinedFull>::Value.has_value());
    EXPECT_EQ(ComponentEditorVisual<DefinedFull>::Value->AssetPath, "camera.glb");
    EXPECT_FALSE(ComponentEditorVisual<DefinedNoChunk>::Value.has_value());
}

// ─── Precedence: the property the whole migration rests on ───────────────────

TEST(ComponentDefinitionProjection, AHandwrittenSchemaOutranksTheProjection)
{
    EXPECT_EQ(TypeSchema<DefinedButOverridden>::Name, "FromHand");

    const auto& fields = RuntimeFieldsOf<DefinedButOverridden>();
    ASSERT_EQ(fields.size(), 1u);
    EXPECT_EQ(fields[0].Name, "handwritten")
        << "a component keeps its handwritten schema until that schema is deleted, "
           "which is what makes the migration incremental";
}

// ─── ComponentSet ────────────────────────────────────────────────────────────

using SetA = ComponentSet<DefinedFull, DefinedIdentityOnly>;
using SetB = ComponentSet<DefinedNoChunk, DefinedButOverridden>;

TEST(ComponentSetTest, KeepsItsOrderAndAnswersByTypeAndId)
{
    static_assert(SetA::Size == 2);
    static_assert(SetA::Contains_v<DefinedFull>);
    static_assert(!SetA::Contains_v<DefinedNoChunk>);

    // Order is load-bearing: it fixes the dense component index and the wire key.
    EXPECT_EQ(SetA::Ids()[0], ResolveComponentTypeId<DefinedFull>());
    EXPECT_EQ(SetA::Ids()[1], ResolveComponentTypeId<DefinedIdentityOnly>());

    EXPECT_TRUE(SetA::Contains(ResolveComponentTypeId<DefinedIdentityOnly>()));
    EXPECT_FALSE(SetA::Contains(ResolveComponentTypeId<DefinedNoChunk>()));
}

// Ownership is checked over the type lists, because registration is idempotent:
// a component named by two sets still yields one World entry, and by then the
// second listing has left no trace.
TEST(ComponentSetTest, OwnershipIsCheckedAcrossTheCollection)
{
    static_assert(ComponentSetCollection<SetA, SetB>::Owned,
                  "disjoint vocabularies must be accepted");
    static_assert(!ComponentSetCollection<SetA, SetA>::Owned,
                  "a component in two vocabularies must be rejected");

    using Flattened = ComponentSetCollection<SetA, SetB>::Flattened;
    static_assert(Flattened::Size == 4, "flattening preserves every member");
}
