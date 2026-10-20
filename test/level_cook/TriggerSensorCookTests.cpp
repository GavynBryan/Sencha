#include "document/DocumentCook.h"
#include "document/DocumentCookSnapshot.h"
#include "document/DocumentSerialization.h"
#include "document/EditorDocument.h"
#include "document/TriggerSensorCook.h"
#include "brush/BrushOps.h"

#include <authored/WorldVocabulary.h>
#include <assets/runtime/RuntimeAssets.h>
#include <ecs/WorldComponentSchema.h>
#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <core/logging/LoggingProvider.h>
#include <logic/TriggerPrograms.h>
#include <physics/CharacterMover.h>
#include <physics/PhysicsQueries.h>
#include <physics/PhysicsWorld.h>
#include <physics/RigidBodyBinding.h>
#include <physics/SensorOverlaps.h>
#include <render/StaticMeshComponent.h>
#include <world/RuntimeComponentSchema.h>
#include <world/build/EntityBuildPackage.h>
#include <world/identity/PersistentEntityIndex.h>
#include <world/scene/SmapFormat.h>
#include <world/serialization/SceneSerializer.h>
#include <world/transform/TransformPropagation.h>
#include <zone/ZonePackageImporter.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace
{
namespace fs = std::filesystem;

class TriggerSensorCookTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite() { RegisterDocumentSerializers(); }
    void SetUp() override
    {
        Root = fs::temp_directory_path() / ("sencha_trigger_cook_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        fs::remove_all(Root);
        fs::create_directories(Root / "materials/dev");
        std::ofstream(Root / "materials/dev/gray.smat") << "{}";
        Document = std::make_unique<EditorDocument>(Logging);
        Document->SetDefaultMaterial({ AssetType::Material, "asset://materials/dev/gray.smat" });
    }
    void TearDown() override
    {
        std::error_code error;
        fs::remove_all(Root, error);
    }
    EntityId Volume(Vec3d position = {}, Vec3d half = { 1, 1, 1 })
    {
        auto& scene = Document->GetScene();
        const auto entity = scene.CreateBrush(position, half);
        scene.GetRegistry().Components.AddComponent(entity, BrushVolume{});
        scene.GetRegistry().Components.AddComponent(entity, Trigger{});
        return entity;
    }
    PersistentEntityId Identity(EntityId entity)
    {
        return Document->GetRegistry().Components.TryGet<PersistentIdComponent>(entity)->Id;
    }
    std::optional<DocumentCookInput> Collect(std::string* error = nullptr)
    {
        return CollectDocumentCookInput(*Document, Root, 16, Logging, nullptr, {}, {}, {}, error);
    }
    LoggingProvider Logging;
    fs::path Root;
    std::unique_ptr<EditorDocument> Document;
};
}

TEST_F(TriggerSensorCookTest, SavedBrushCooksImportsAndActivatesCharacterVirtualAtItsAuthoredShape)
{
    auto& scene = Document->GetScene();
    const auto parent = scene.CreateBrush({ 10, 0, 5 }, { 1, 0.25f, 1 });
    Transform3f parentPose = Transform3f::Identity();
    parentPose.Position = { 10, 0, 5 };
    parentPose.Rotation = Quatf::FromAxisAngle(Vec3d::Up(), 0.6f);
    scene.SetTransform(parent, parentPose);
    const auto entity = Volume({ 1, 3, -2 }, { 0.5f, 1, 2 });
    ASSERT_TRUE(scene.SetParent(entity, parent));
    auto local = *scene.TryGetLocalTransform(entity);
    local.Rotation = Quatf::FromAxisAngle(Vec3d{ 0, 0, 1 }, 0.4f);
    local.Scale = { 2, 1, 0.5f };
    scene.SetTransform(entity, local);
    scene.SetBrushMesh(entity, BrushOps::Translate(*scene.TryGetBrushMesh(entity), { 2, 0, 0 }));
    const Transform3f authoredFrame = scene.ComposeWorldTransform(entity);
    const Vec3d center = authoredFrame.TransformPoint({ 2, 0, 0 });
    const auto identity = Identity(entity), parentId = Identity(parent);
    auto source = JsonParse(R"({"version":1,"commands":[{"enabled":false,"verb":"game.unavailable",
        "arguments":{"future":{"asset":"asset://missing/unused.smat"}},"future_metadata":[1,true]}]})");
    ASSERT_TRUE(source);
    ASSERT_TRUE(SetTriggerProgram(scene.GetRegistry().Components, entity, *source));
    const auto path = Root / "levels/sensor.sscene";
    fs::create_directories(path.parent_path());
    ASSERT_TRUE(Document->SaveAs(path.generic_string()));
    const auto result = CookDocument(path, Root, 16, &Logging);
    ASSERT_TRUE(result.Success) << result.Error;
    EXPECT_EQ(JsonStringify(*TriggerProgramSource(Document->GetRegistry().Components, entity)), JsonStringify(*source));

    ComponentSerializerRegistry serializers;
    RegisterEngineSceneSerializers(serializers);
    RuntimeAssets assets(Logging, serializers);
    ASSERT_TRUE(ScanAssetsDirectory((Root / ".cooked").generic_string(), assets.Registry, assets.Assets.Kinds()));
    SmapContents contents;
    SmapError error;
    ASSERT_TRUE(ReadSmapFile(result.CookedScenePath, serializers, contents, &error)) << error.Message;
    EXPECT_EQ(contents.Collision.size(), 1u) << "only the parent solid contributes collision";
    for (const auto& dependency : contents.Dependencies)
        EXPECT_NE(dependency.Path, "asset://missing/unused.smat");
    EntityBuildPackage package;
    ASSERT_TRUE(BuildEntityPackageFromSmap(contents, serializers, package, &error)) << error.Message;
    WorldComponentSchema schema;
    RegisterEngineRuntimeComponents(schema);
    schema.Seal();
    World runtime;
    schema.Apply(runtime);
    runtime.SetResource(assets.Assets.Stores());
    auto& index = runtime.AddResource<PersistentEntityIndex>();
    SceneSerializationContext context(Logging, &assets.Assets);
    ZoneImportError importError;
    ASSERT_TRUE(ImportPackageIntoPartition(runtime, schema, package, StoragePartitionId::Default(), {}, serializers,
                                          context, &importError)) << importError.Message;
    const auto trigger = index.TryResolve(identity), runtimeParent = index.TryResolve(parentId);
    ASSERT_TRUE(trigger.IsValid());
    ASSERT_TRUE(runtimeParent.IsValid());
    EXPECT_EQ(runtime.TryGet<Parent>(trigger)->Entity, runtimeParent);
    const auto* collider = std::as_const(runtime).TryGet<Collider>(trigger);
    ASSERT_NE(collider, nullptr);
    EXPECT_TRUE(collider->IsTrigger);
    EXPECT_FALSE(runtime.HasComponent<StaticMeshComponent>(trigger));
    EXPECT_TRUE(TriggerProgramSource(runtime, trigger)->Find("commands")->AsArray().empty());
    PropagateTransforms(runtime);
    EXPECT_TRUE(runtime.TryGet<WorldTransform>(trigger)->Value.NearlyEquals(authoredFrame));
    PhysicsWorld physics;
    RigidBodyBinding binding(physics);
    StoragePartitionSet active;
    active.Add(StoragePartitionId::Default());
    binding.SyncToPhysics(runtime, active);
    EXPECT_EQ(physics.SensorCount(), 1u);
    EXPECT_FALSE(PhysicsQueries(physics).Raycast(center + Vec3d{ 4, 0, 0 }, { -1, 0, 0 }, 8,
                                               PhysicsQueryFilter{}).Hit);
    const auto activator = runtime.CreateEntity();
    CharacterMover mover(physics, { .Radius = 0.2f, .Height = 1.0f }, center);
    SensorOverlaps observations;
    observations.Begin();
    mover.ObserveSensors(activator, observations);
    ASSERT_TRUE(observations.Commit(1));
    ASSERT_EQ(observations.Pairs().size(), 1u);
    EXPECT_EQ(observations.Pairs()[0], (SensorPair{ trigger, activator }));
    runtime.TryGet<LocalTransform>(runtimeParent)->Value.Position.X += 20;
    PropagateTransforms(runtime);
    binding.SyncToPhysics(runtime, active);
    observations.Begin();
    mover.ObserveSensors(activator, observations);
    ASSERT_TRUE(observations.Commit(2));
    ASSERT_EQ(observations.Occurrences().size(), 1u);
    EXPECT_EQ(observations.Occurrences()[0].Transition, SensorTransition::Exit);
}

TEST_F(TriggerSensorCookTest, BrushEditsDetermineSensorDimensionsAndNeverAddAnEditableCollider)
{
    const auto entity = Volume({}, { 1, 2, 3 });
    std::vector<CookTriggerSensor> sensors;
    std::vector<std::string> errors;
    ASSERT_TRUE(CollectTriggerSensors(Document->GetScene(), sensors, errors));
    ASSERT_EQ(sensors.size(), 1u);
    const auto first = sensors.front().Sensor;
    Document->GetScene().SetBrushMesh(entity, BrushOps::Translate(BrushOps::MakeBox({ 4, 2, 3 }), { 7, 0, 0 }));
    sensors.clear();
    ASSERT_TRUE(CollectTriggerSensors(Document->GetScene(), sensors, errors));
    ASSERT_EQ(sensors.size(), 1u);
    EXPECT_NE(sensors.front().Sensor.Shape, first.Shape);
    EXPECT_NEAR(sensors.front().Sensor.ShapePose.Position.X, 7, 1e-5f);
    EXPECT_FALSE(Document->GetRegistry().Components.HasComponent<Collider>(entity));
    auto input = Collect();
    ASSERT_TRUE(input);
    EXPECT_TRUE(input->Snapshot().Brushes.empty());
    EXPECT_EQ(input->Snapshot().PassthroughScene.Find("entities")->Size(), 1u);
}

TEST_F(TriggerSensorCookTest, UnsupportedGeometryAndDuplicateShapeOwnershipRefuseTheCook)
{
    const auto entity = Volume();
    auto& scene = Document->GetScene();
    for (auto mesh : { BrushOps::MakeCylinder({ 1, 1, 1 }, 1, 12), BrushOps::MakePlane({ 1, 1, 1 }, 1) })
    {
        scene.SetBrushMesh(entity, std::move(mesh));
        std::string error;
        EXPECT_FALSE(Collect(&error));
        EXPECT_NE(error.find("trigger geometry"), std::string::npos);
    }
    scene.SetBrushMesh(entity, BrushOps::MakeBox({ 1, 1, 1 }));
    scene.SetBrushModifiers(entity, { { .Params = ArrayModifier{} } });
    std::string error;
    EXPECT_FALSE(Collect(&error));
    EXPECT_NE(error.find("multiple pieces"), std::string::npos);
    scene.SetBrushModifiers(entity, {});
    scene.GetRegistry().Components.AddComponent(entity, Collider{});
    EXPECT_FALSE(Collect(&error));
    EXPECT_NE(error.find("separately authored Collider"), std::string::npos);
}

TEST_F(TriggerSensorCookTest, ExplicitEntityBindingsRetainReferencedBrushIdentity)
{
    const auto trigger = Volume();
    const auto target = Document->GetScene().CreateBrush({ 5, 0, 0 });
    const auto id = Identity(target);
    auto& world = Document->GetScene().GetRegistry().Components;
    VerbRegistrationScope scope(*FindVerbRegistry(world), "test.target");
    VerbDefinition verb;
    verb.Name = "test.target";
    verb.Arguments.Children = { MakeDataField(DataFieldKind::Entity, "target", "Target") };
    (void)scope.Declare(std::move(verb));
    ASSERT_TRUE(scope.Commit());
    auto source = EmptyTriggerProgram();
    source.Find("commands")->AsArray().emplace_back(JsonValue::Object{
        { "verb", "test.target" }, { "arguments", JsonValue::Object{
            { "target", JsonValue::Object{ { "entity", PersistentEntityIdToString(id) } } } } } });
    ASSERT_TRUE(SetTriggerProgram(world, trigger, source));
    auto input = Collect();
    ASSERT_TRUE(input);
    EXPECT_EQ(input->Snapshot().PassthroughScene.Find("entities")->Size(), 2u);
}

TEST_F(TriggerSensorCookTest, ShapeExtractionDoesNotLosePrecisionThroughWorldTranslationOrRotation)
{
    const auto entity = Volume({}, { 0.125f, 1.5f, 2.75f });
    auto& scene = Document->GetScene();
    scene.SetBrushMesh(entity, BrushOps::Translate(*scene.TryGetBrushMesh(entity), { 2, 0, 0 }));
    std::vector<CookTriggerSensor> initial;
    std::vector<std::string> errors;
    ASSERT_TRUE(CollectTriggerSensors(scene, initial, errors));
    ASSERT_EQ(initial.size(), 1u);
    auto transform = *scene.TryGetLocalTransform(entity);
    transform.Position = { 10000000, -10000000, 10000000 };
    transform.Rotation = Quatf::FromAxisAngle(Vec3d{ 1, 2, 3 }.Normalized(), 0.7f);
    scene.SetTransform(entity, transform);
    std::vector<CookTriggerSensor> relocated;
    ASSERT_TRUE(CollectTriggerSensors(scene, relocated, errors));
    ASSERT_EQ(relocated.size(), 1u);
    EXPECT_EQ(relocated[0].Sensor.Shape, initial[0].Sensor.Shape);
    EXPECT_EQ(relocated[0].Sensor.ShapePose, initial[0].Sensor.ShapePose);
}
