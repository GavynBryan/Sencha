// A sensor volume brush reaches the cooked level as its own entity -- identity,
// transform and components intact -- carrying a sensor collider fitted to the
// box it is, and never as geometry. A volume that is not exactly a box is
// refused by name rather than approximated.

#include "brush/BrushOps.h"
#include "document/BrushComponents.h"
#include "document/DocumentCook.h"
#include "document/DocumentCookRequest.h"
#include "document/DocumentCookSnapshot.h"
#include "document/DocumentSerialization.h"
#include "document/EditorDocument.h"
#include "document/EntityNameComponent.h"

#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/World.h>
#include <logic/Trigger.h>
#include <logic/TriggerSequences.h>
#include <project/CookProfile.h>
#include <world/identity/PersistentIdComponent.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <optional>
#include <string>

namespace
{
namespace fs = std::filesystem;

constexpr std::string_view kSequence = R"({"conditions": [], "commands": [
    {"verb": "movement.launch", "arguments": {"target": {"port": "activator"}, "speed": 12}}]})";

class TriggerVolumeCookTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite() { RegisterDocumentSerializers(); }

    void SetUp() override
    {
        Root = fs::temp_directory_path()
            / ("sencha_trigger_cook_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        fs::remove_all(Root);
        fs::create_directories(Root / "materials/dev");
        std::ofstream(Root / "materials/dev/gray.smat", std::ios::trunc) << "{}";
        Doc.SetDefaultMaterial(AssetRef{ AssetType::Material, "asset://materials/dev/gray.smat" });
        Profile.Id = "collision_only_test";
        Profile.Name = "Collision Only Test";
        Profile.TargetSteps = { std::string(CookStepIds::Collision) };
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(Root, ec);
    }

    World& Entities() { return Doc.GetScene().GetRegistry().Components; }

    EntityId TriggerVolume(const Transform3f& transform, BrushMesh mesh, std::string_view name)
    {
        const EntityId entity = Doc.GetScene().CreateBrushFromMesh(transform, std::move(mesh));
        Entities().AddComponent(entity, BrushVolume{});
        Entities().AddComponent(entity, VolumeSensor{});
        Entities().AddComponent(entity, Trigger{});
        if (EntityNameComponent* named = Entities().TryGet<EntityNameComponent>(entity))
            named->Value = InlineString<64>(name);
        else
            Entities().AddComponent(entity, EntityNameComponent{ InlineString<64>(name) });
        EXPECT_TRUE(SetTriggerSequence(Entities(), entity, *JsonParse(kSequence)));
        return entity;
    }

    std::optional<DocumentCookInput> Cook(std::string* error = nullptr)
    {
        Doc.GetScene().RefreshDerivedTransforms();
        return CollectDocumentCookInput(Doc, Root, 16.0, Logging, nullptr, {}, {},
                                        DocumentCookOptions{ .Profile = &Profile }, error);
    }

    // The cooked entity carrying `entity`'s persistent identity.
    static const JsonValue* CookedEntity(const DocumentCookInput& input, const World& world, EntityId entity)
    {
        const PersistentIdComponent* id = world.TryGet<PersistentIdComponent>(entity);
        const JsonValue* entities = input.Snapshot().PassthroughScene.Find("entities");
        if (id == nullptr || entities == nullptr)
            return nullptr;
        const std::string wanted = PersistentEntityIdToString(id->Id);
        for (const JsonValue& cooked : entities->AsArray())
        {
            const JsonValue* components = cooked.Find("components");
            const JsonValue* persistent = components != nullptr ? components->Find("persistent_id") : nullptr;
            const JsonValue* text = persistent != nullptr ? persistent->Find("id") : nullptr;
            if (text != nullptr && text->IsString() && text->AsString() == wanted)
                return &cooked;
        }
        return nullptr;
    }

    fs::path Root;
    LoggingProvider Logging;
    EditorDocument Doc{ Logging };
    CookProfile Profile;
};

BrushMesh Shifted(BrushMesh mesh, Vec3d by)
{
    for (BrushVertex& vertex : mesh.Vertices)
        vertex.Position = vertex.Position + by;
    return mesh;
}

Vec3d VectorOf(const JsonValue& value)
{
    const auto& array = value.AsArray();
    return Vec3d(static_cast<float>(array[0].AsNumber()), static_cast<float>(array[1].AsNumber()),
                 static_cast<float>(array[2].AsNumber()));
}
} // namespace

TEST_F(TriggerVolumeCookTest, ATriggerVolumeCooksToItsEntityWithAFittedSensorAndNoGeometry)
{
    (void)Doc.GetScene().CreateBrush(Vec3d{ 0, -0.5, 0 }, Vec3d{ 8, 0.5, 8 });
    Transform3f transform = Transform3f::Identity();
    transform.Position = Vec3d(2.0f, 0.0f, 3.0f);
    transform.Rotation = Quatf::FromAxisAngle(Vec3d(0, 1, 0), std::numbers::pi_v<float> / 3);
    transform.Scale = Vec3d(2.0f, 1.0f, 1.0f);
    // Drawn off its origin: the box sits a quarter metre up, as a floor pad does.
    const EntityId pad = TriggerVolume(transform, Shifted(BrushOps::MakeBox(Vec3d(1.0f, 0.25f, 0.5f)), Vec3d(0, 0.25f, 0)),
                                       "launch pad");

    std::string error;
    const std::optional<DocumentCookInput> input = Cook(&error);
    ASSERT_TRUE(input.has_value()) << error;
    EXPECT_EQ(input->Snapshot().Brushes.size(), 1u) << "the trigger volume was cooked as geometry";

    const JsonValue* cooked = CookedEntity(*input, Entities(), pad);
    ASSERT_NE(cooked, nullptr) << "the trigger volume's entity did not reach the level";
    const JsonValue& components = *cooked->Find("components");
    for (const char* editorOnly : { "brush", "volume", "volume_sensor", "name" })
        EXPECT_EQ(components.Find(editorOnly), nullptr) << editorOnly;

    const JsonValue* trigger = components.Find("trigger");
    ASSERT_NE(trigger, nullptr);
    ASSERT_NE(trigger->Find("sequence"), nullptr);
    EXPECT_EQ(JsonStringify(*trigger->Find("sequence")), JsonStringify(*JsonParse(kSequence)));

    const JsonValue* collider = components.Find("collider");
    ASSERT_NE(collider, nullptr);
    EXPECT_TRUE(collider->Find("is_trigger")->AsBool());
    const JsonValue& shape = *collider->Find("shape");
    EXPECT_EQ(shape.Find("type")->AsString(), "box");
    // The entity's scale is in the box; its turn and position stay the body's.
    const Vec3d half = VectorOf(*shape.Find("half_extents"));
    EXPECT_NEAR(half.X, 2.0f, 1e-4f);
    EXPECT_NEAR(half.Y, 0.25f, 1e-4f);
    EXPECT_NEAR(half.Z, 0.5f, 1e-4f);
    const Vec3d centre = VectorOf(*shape.Find("center"));
    EXPECT_NEAR(centre.X, 0.0f, 1e-4f);
    EXPECT_NEAR(centre.Y, 0.25f, 1e-4f);
    EXPECT_NEAR(centre.Z, 0.0f, 1e-4f);
}

TEST_F(TriggerVolumeCookTest, AVolumeThatIsNotABoxRefusesTheCookByName)
{
    (void)TriggerVolume(Transform3f::Identity(), BrushOps::MakeCylinder(Vec3d(1, 1, 1), 1, 8), "round trigger");
    std::string error;
    EXPECT_FALSE(Cook(&error).has_value());
    EXPECT_NE(error.find("round trigger"), std::string::npos) << error;
    EXPECT_NE(error.find("box"), std::string::npos) << error;
}

TEST_F(TriggerVolumeCookTest, AVolumeWithoutASensorStaysAir)
{
    const EntityId room = Doc.GetScene().CreateBrush(Vec3d{ 0, 1.5, 0 }, Vec3d{ 4, 1.5, 4 });
    Entities().AddComponent(room, BrushVolume{});
    const std::optional<DocumentCookInput> input = Cook();
    ASSERT_TRUE(input.has_value());
    EXPECT_EQ(CookedEntity(*input, Entities(), room), nullptr);
    EXPECT_TRUE(input->Snapshot().Brushes.empty());
}

TEST_F(TriggerVolumeCookTest, AResizedTriggerVolumeCooksItsNewBox)
{
    const EntityId pad = TriggerVolume(Transform3f::Identity(), BrushOps::MakeBox(Vec3d(1.0f, 1.0f, 1.0f)), "pad");
    Doc.GetScene().SetBrushHalfExtents(pad, Vec3d(3.0f, 0.5f, 2.0f));

    const std::optional<DocumentCookInput> input = Cook();
    ASSERT_TRUE(input.has_value());
    const JsonValue* cooked = CookedEntity(*input, Entities(), pad);
    ASSERT_NE(cooked, nullptr);
    const Vec3d half = VectorOf(*cooked->Find("components")->Find("collider")->Find("shape")->Find("half_extents"));
    EXPECT_NEAR(half.X, 3.0f, 1e-4f);
    EXPECT_NEAR(half.Y, 0.5f, 1e-4f);
    EXPECT_NEAR(half.Z, 2.0f, 1e-4f);
}
