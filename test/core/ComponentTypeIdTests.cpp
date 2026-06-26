#include <ecs/ComponentTypeId.h>

#include <gtest/gtest.h>

#include <string_view>

// ─── Identity sources, exercised three ways ──────────────────────────────────

// Explicit key only.
struct CtidKeyOnly {};
SENCHA_DECLARE_COMPONENT_TYPE(CtidKeyOnly, "test.ctid_key_only");

// TypeSchema name only.
struct CtidSchemaOnly {};
template <>
struct TypeSchema<CtidSchemaOnly>
{
    static constexpr std::string_view Name = "test.ctid_schema_only";
};

// Both present — the explicit key must win over the schema name.
struct CtidBoth {};
template <>
struct TypeSchema<CtidBoth>
{
    static constexpr std::string_view Name = "test.ctid_both_schema";
};
SENCHA_DECLARE_COMPONENT_TYPE(CtidBoth, "test.ctid_both_key");

// ─── The hash itself ─────────────────────────────────────────────────────────

TEST(ComponentTypeId, HashIsConstexprAndStable)
{
    static_assert(MakeComponentTypeId("a") == MakeComponentTypeId("a"),
                  "same name must fold to the same constexpr id");
    static_assert(MakeComponentTypeId("a") != MakeComponentTypeId("b"),
                  "different names must differ");
    EXPECT_EQ(MakeComponentTypeId("sencha.local_transform"),
              MakeComponentTypeId("sencha.local_transform"));
    EXPECT_NE(MakeComponentTypeId("sencha.local_transform"),
              MakeComponentTypeId("sencha.world_transform"));
}

TEST(ComponentTypeId, NeverHandsOutTheInvalidSentinel)
{
    EXPECT_TRUE(MakeComponentTypeId("anything").IsValid());
    EXPECT_TRUE(MakeComponentTypeId("").IsValid()); // empty still maps off zero
}

// ─── Resolution priority ─────────────────────────────────────────────────────

TEST(ComponentTypeId, ResolvePicksKeyOverSchema)
{
    EXPECT_EQ(ResolveComponentTypeId<CtidKeyOnly>(),  MakeComponentTypeId("test.ctid_key_only"));
    EXPECT_EQ(ResolveComponentTypeId<CtidSchemaOnly>(), MakeComponentTypeId("test.ctid_schema_only"));
    EXPECT_EQ(ResolveComponentTypeId<CtidBoth>(),     MakeComponentTypeId("test.ctid_both_key"));
    EXPECT_NE(ResolveComponentTypeId<CtidBoth>(),     MakeComponentTypeId("test.ctid_both_schema"));
}

TEST(ComponentTypeId, NameResolutionMirrorsIdResolution)
{
    EXPECT_EQ(ResolveComponentName<CtidKeyOnly>(),   "test.ctid_key_only");
    EXPECT_EQ(ResolveComponentName<CtidSchemaOnly>(), "test.ctid_schema_only");
    EXPECT_EQ(ResolveComponentName<CtidBoth>(),      "test.ctid_both_key");
}
