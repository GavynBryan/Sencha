#include <core/metadata/Field.h>
#include <core/serialization/FourCC.h>
#include <ecs/ComponentTypeId.h>
#include <world/serialization/ComponentSerializer.h>
#include <world/serialization/ComponentStorageTraits.h>
#include <world/serialization/SceneSerializer.h>

#include <gtest/gtest.h>

#include <memory>
#include <string_view>
#include <tuple>

// ─── Test components, each with a full (Name, SceneChunkId, Fields) schema ────

struct SerP { int V = 0; };
template <> struct TypeSchema<SerP>
{
    static constexpr std::string_view Name        = "test.ser_shared_key";
    static constexpr std::uint32_t    SceneChunkId = MakeFourCC('S','E','R','P');
    static auto Fields() { return std::tuple{ MakeField("v", &SerP::V) }; }
};

// Same JSON key as SerP, but an explicit key override forces a DIFFERENT
// ComponentTypeId, and a different chunk. Partial-overlap → must be rejected.
struct SerQ { int V = 0; };
template <> struct TypeSchema<SerQ>
{
    static constexpr std::string_view Name        = "test.ser_shared_key"; // same JsonKey
    static constexpr std::uint32_t    SceneChunkId = MakeFourCC('S','E','R','Q');
    static auto Fields() { return std::tuple{ MakeField("v", &SerQ::V) }; }
};
SENCHA_DECLARE_COMPONENT_TYPE(SerQ, "test.ser_q_distinct_id");

// Distinct key/id from SerR, but the SAME chunk id → partial overlap.
struct SerR { int V = 0; };
template <> struct TypeSchema<SerR>
{
    static constexpr std::string_view Name        = "test.ser_r";
    static constexpr std::uint32_t    SceneChunkId = MakeFourCC('S','H','R','D');
    static auto Fields() { return std::tuple{ MakeField("v", &SerR::V) }; }
};
struct SerS { int V = 0; };
template <> struct TypeSchema<SerS>
{
    static constexpr std::string_view Name        = "test.ser_s";
    static constexpr std::uint32_t    SceneChunkId = MakeFourCC('S','H','R','D'); // same chunk
    static auto Fields() { return std::tuple{ MakeField("v", &SerS::V) }; }
};

namespace
{
    template <typename T>
    std::unique_ptr<IComponentSerializer> Ser()
    {
        return std::make_unique<ComponentSerializer<T>>();
    }
}

TEST(SceneSerializerIdentity, IdenticalRegistrationIsIdempotent)
{
    ClearComponentSerializers();
    RegisterComponentSerializer(Ser<SerP>());
    const size_t afterFirst = GetComponentSerializerEntries().size();
    RegisterComponentSerializer(Ser<SerP>()); // full tuple match → no-op
    const size_t afterSecond = GetComponentSerializerEntries().size();
    EXPECT_EQ(afterFirst, afterSecond);
    ClearComponentSerializers();
}

TEST(SceneSerializerIdentity, FullTupleIsConsistent)
{
    // Sanity: SerP's three identity facets are exactly what we expect.
    ComponentSerializer<SerP> s;
    EXPECT_EQ(s.TypeId(), MakeComponentTypeId("test.ser_shared_key"));
    EXPECT_EQ(s.JsonKey(), "test.ser_shared_key");
    EXPECT_EQ(s.BinaryChunkId(), MakeFourCC('S','E','R','P'));
}

TEST(SceneSerializerIdentity, SameJsonKeyDifferentTypeIdRejected)
{
#ifdef NDEBUG
    GTEST_SKIP() << "Collision assertion only fires in debug builds.";
#else
    EXPECT_DEATH(
        {
            ClearComponentSerializers();
            RegisterComponentSerializer(Ser<SerP>());
            RegisterComponentSerializer(Ser<SerQ>()); // same key, different id
        },
        "collision");
#endif
}

TEST(SceneSerializerIdentity, SameChunkDifferentTypeIdRejected)
{
#ifdef NDEBUG
    GTEST_SKIP() << "Collision assertion only fires in debug builds.";
#else
    EXPECT_DEATH(
        {
            ClearComponentSerializers();
            RegisterComponentSerializer(Ser<SerR>());
            RegisterComponentSerializer(Ser<SerS>()); // same chunk, different id
        },
        "collision");
#endif
}
