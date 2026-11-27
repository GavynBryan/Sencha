// The generator's expected output, compiled and exercised.
//
// The golden file is what sencha-component-codegen must emit for
// GoldenComponent.h. Compiling it here proves the emitted shape is valid C++
// that projects correctly; a byte-comparison against a real run is the
// generator's own test, and needs the tool built.

#include "component_codegen/GoldenComponent.h"
#include "component_codegen/GoldenComponent.sencha.h.expected"

#include <core/logging/LoggingProvider.h>
#include <core/metadata/ComponentPersistence.h>
#include <core/metadata/ComponentRemovable.h>
#include <core/metadata/EditorVisual.h>
#include <core/metadata/RuntimeSchema.h>
#include <core/metadata/TypeSchema.h>
#include <core/serialization/JsonArchive.h>
#include <ecs/ComponentTypeId.h>
#include <math/Angle.h>
#include <ecs/World.h>
#include <net/ReplicationLayout.h>
#include <world/serialization/ComponentSerializer.h>

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
    EXPECT_EQ(ComponentPersistenceOf<CodegenGolden>::Value, ComponentPersistence::Saved);
    EXPECT_EQ(ComponentPersistenceOf<CodegenGoldenTag>::Value, ComponentPersistence::Authored);

    ASSERT_TRUE(ComponentEditorVisual<CodegenGolden>::Value.has_value());
    EXPECT_EQ(ComponentEditorVisual<CodegenGolden>::Value->AssetPath, "golden.glb");
}

// Presence is the tag's whole value, and a schema is what lets a scene state
// that presence.
TEST(GeneratedComponentMetadata, AFieldlessSchemaStillNamesASceneChunk)
{
    EXPECT_EQ(TypeSchema<CodegenGoldenTag>::Name, "CodegenGoldenTag");
    EXPECT_EQ(TypeSchema<CodegenGoldenTag>::SceneChunkId, MakeFourCC('G', 'T', 'A', 'G'));
    EXPECT_TRUE(RuntimeFieldsOf<CodegenGoldenTag>().empty());
}

TEST(GeneratedComponentMetadata, BindsAssetFieldsByKindArityAndSubtype)
{
    const RuntimeField* mesh = nullptr;
    const RuntimeField* materials = nullptr;
    const RuntimeField* profile = nullptr;
    for (const RuntimeField& field : RuntimeFieldsOf<CodegenGoldenAssets>())
    {
        if (field.Name == "mesh")           mesh = &field;
        else if (field.Name == "materials") materials = &field;
        else if (field.Name == "profile")   profile = &field;
    }

    ASSERT_NE(mesh, nullptr);
    EXPECT_EQ(mesh->Asset, AssetType::StaticMesh);
    EXPECT_EQ(mesh->Arity, AssetArity::Single);

    ASSERT_NE(materials, nullptr);
    EXPECT_EQ(materials->Asset, AssetType::Material);
    EXPECT_EQ(materials->Arity, AssetArity::List);

    ASSERT_NE(profile, nullptr);
    EXPECT_EQ(profile->Asset, AssetType::Data);
    EXPECT_EQ(profile->DataSubtype, kGoldenProfileSubtype);
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

// A member initializer that calls a constexpr function is a default like any
// other: the schema carries the radians the initializer stores.
TEST(GeneratedComponentMetadata, ADefaultWrittenAsAConversionIsTheSchemaDefault)
{
    const auto fields = TypeSchema<CodegenGolden>::Fields();
    const auto& pitch = std::get<2>(fields);
    ASSERT_EQ(pitch.Name, "pitch");
    ASSERT_TRUE(pitch.DefaultValue.has_value());
    EXPECT_EQ(*pitch.DefaultValue, ToRadians(30.0f));
    EXPECT_EQ(*pitch.DefaultValue, CodegenGolden{}.Pitch);
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

TEST(GeneratedComponentMetadata, ARunTimeFieldIsWrittenOnlyInASaveAndAnUnsavedOneOnlyInAScene)
{
    World world;
    world.RegisterComponent<CodegenGolden>();
    const EntityId entity = world.CreateEntity();
    CodegenGolden golden;
    golden.Engaged = true;
    golden.Tethered = true;
    world.AddComponent<CodegenGolden>(entity, golden);
    ComponentSerializer<CodegenGolden> serializer;
    LoggingProvider logging;
    SceneSerializationContext context(logging);

    JsonWriteArchive authored;
    ASSERT_TRUE(serializer.Save(authored, entity, world, context));
    const JsonValue scene = authored.TakeValue();
    EXPECT_EQ(scene.Find("engaged"), nullptr);
    ASSERT_NE(scene.Find("tethered"), nullptr);

    context.Purpose = SceneRecordPurpose::Save;
    JsonWriteArchive saving;
    ASSERT_TRUE(serializer.Save(saving, entity, world, context));
    const JsonValue saved = saving.TakeValue();
    ASSERT_NE(saved.Find("engaged"), nullptr);
    EXPECT_TRUE(saved.Find("engaged")->AsBool());
    EXPECT_EQ(saved.Find("tethered"), nullptr);

    JsonWriteArchive defaults;
    ASSERT_TRUE(serializer.SaveDefault(defaults));
    const JsonValue defaulted = defaults.TakeValue();
    EXPECT_NE(defaulted.Find("engaged"), nullptr);
    EXPECT_EQ(defaulted.Find("tethered"), nullptr);

    EXPECT_TRUE(serializer.Authorable());
    bool offersEngaged = false;
    bool offersTethered = false;
    for (const RuntimeField& field : serializer.RuntimeFields())
    {
        offersEngaged = offersEngaged || field.Name == "engaged";
        offersTethered = offersTethered || field.Name == "tethered";
    }
    EXPECT_FALSE(offersEngaged) << "a tool offers a run-time field";
    EXPECT_TRUE(offersTethered);

    const EntityId restored = world.CreateEntity();
    JsonReadArchive reading(saved);
    ASSERT_TRUE(serializer.LoadIntoWorld(reading, restored, world, context));
    EXPECT_TRUE(world.TryGet<CodegenGolden>(restored)->Engaged);
    EXPECT_FALSE(world.TryGet<CodegenGolden>(restored)->Tethered) << "a save that leaves it out reads its default";
}
