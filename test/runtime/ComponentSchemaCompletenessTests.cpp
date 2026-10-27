// Deliberately includes nothing that teaches the schema about math types: a
// component's fields must flatten the same in every translation unit, or the
// one the linker keeps decides its scene fingerprint for the whole binary.

#include <render/StaticMeshComponent.h>

#include <gtest/gtest.h>

#include <algorithm>

// Checked here, where only the component is included, so the linker's choice
// of instantiation cannot hide a header that left the trait out.
static_assert(PackedScalarGroup<decltype(StaticMeshComponent::LightmapScaleBias)>::Count == 4,
              "a component header must bring the schema traits of its math members");

TEST(ComponentSchemaCompleteness, AMathMemberFlattensWithOnlyItsComponentIncluded)
{
    const std::vector<RuntimeField>& fields = RuntimeFieldsOf<StaticMeshComponent>();
    const auto scaleBias = std::find_if(fields.begin(), fields.end(),
                                        [](const RuntimeField& field) { return field.Name == "lightmap_scale_bias"; });
    ASSERT_NE(scaleBias, fields.end());
    EXPECT_EQ(scaleBias->Scalar, FieldScalar::Float);
    EXPECT_EQ(scaleBias->Count, 4u);
}
