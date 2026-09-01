// The generator's expected output, compiled and exercised.
//
// The golden file is what sencha-component-codegen must emit for
// GoldenComponent.h. Compiling it here proves the emitted shape is valid C++
// that projects correctly; a byte-comparison against a real run is the
// generator's own test, and needs the tool built.

#include "component_codegen/GoldenComponent.h"
#include "component_codegen/GoldenComponent.sencha.h.expected"

#include <core/metadata/ComponentRemovable.h>
#include <core/metadata/EditorVisual.h>
#include <core/metadata/RuntimeSchema.h>
#include <core/metadata/TypeSchema.h>
#include <ecs/ComponentTypeId.h>
#include <net/ReplicationLayout.h>

#include <gtest/gtest.h>

TEST(GeneratedComponentMetadata, ProjectsIntoEveryConsumer)
{
    EXPECT_EQ(ResolveComponentTypeId<CodegenGolden>(),
              MakeComponentTypeId("test.codegen.golden"));
    EXPECT_EQ(TypeSchema<CodegenGolden>::Name, "CodegenGolden");
    EXPECT_EQ(TypeSchema<CodegenGolden>::SceneChunkId, MakeFourCC('G', 'O', 'L', 'D'));
    EXPECT_TRUE(TypeSchema<CodegenGolden>::Replicated);
    EXPECT_TRUE(TypeSchema<CodegenGolden>::Predicted);
    EXPECT_FALSE(ComponentRemovable<CodegenGolden>::Value);

    ASSERT_TRUE(ComponentEditorVisual<CodegenGolden>::Value.has_value());
    EXPECT_EQ(ComponentEditorVisual<CodegenGolden>::Value->AssetPath, "golden.glb");
}

TEST(GeneratedComponentMetadata, CarriesEveryFieldAnnotation)
{
    const auto& fields = RuntimeFieldsOf<CodegenGolden>();

    const RuntimeField* speed = nullptr;
    const RuntimeField* pitch = nullptr;
    const RuntimeField* derived = nullptr;
    const RuntimeField* tint = nullptr;
    for (const RuntimeField& field : fields)
    {
        if (field.Name == "speed")        speed = &field;
        else if (field.Name == "pitch")   pitch = &field;
        else if (field.Name == "derived") derived = &field;
        else if (field.Name == "tint")    tint = &field;
    }

    ASSERT_NE(speed, nullptr);
    EXPECT_TRUE(speed->OwnerOnly);
    EXPECT_EQ(speed->Quantization.Bits, 16);

    ASSERT_NE(pitch, nullptr);
    EXPECT_TRUE(pitch->DisplayDegrees);

    ASSERT_NE(derived, nullptr);
    EXPECT_TRUE(derived->LocalOnly);

    ASSERT_NE(tint, nullptr);
    EXPECT_EQ(tint->Label, "Tint");
    EXPECT_EQ(tint->Scalar, FieldScalar::Color3) << "a colour collapses to one field";
}

// The property that makes adding a member safe: an untagged member is invisible
// to the schema, so it changes neither the scene format nor the wire layout.
TEST(GeneratedComponentMetadata, AnUntaggedMemberIsNotInTheSchema)
{
    for (const RuntimeField& field : RuntimeFieldsOf<CodegenGolden>())
        EXPECT_NE(field.Name, "Scratch");

    ReplicationLayout layout;
    layout.Add<CodegenGolden>();
    ASSERT_EQ(layout.Error(), ReplicationLayoutError::None)
        << ReplicationLayoutErrorToString(layout.Error()) << ": " << layout.ErrorDetail();

    const ReplicatedComponent* replicated =
        layout.Find(ResolveComponentTypeId<CodegenGolden>());
    ASSERT_NE(replicated, nullptr);
    EXPECT_TRUE(replicated->Predicted);
}
