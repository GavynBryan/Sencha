#include "document/DocumentSerialization.h"
#include "document/EditorDocument.h"
#include "document/EditorScene.h"
#include "document/commands/DeleteEntityCommand.h"
#include "selection/SelectionContext.h"
#include "selection/SelectionService.h"

#include <core/logging/LoggingProvider.h>
#include <render/PointLightComponent.h>
#include <render/SpotLightComponent.h>
#include <world/transform/TransformComponents.h>
#include <world/identity/PersistentIdComponent.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace
{
namespace fs = std::filesystem;

class PrefabExtendsProjectionTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite() { RegisterDocumentSerializers(); }

    void SetUp() override
    {
        Root = fs::temp_directory_path()
            / ("sencha_prefab_extends_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        fs::remove_all(Root);
        fs::create_directories(Root / "props");

        EditorDocument base(Logging);
        const EntityId post = base.GetScene().CreateEntity(Vec3d{ 0, 0, 0 });
        const EntityId light = base.GetScene().CreateEntity(Vec3d{ 0, 2, 0 });
        PointLightComponent lamp{};
        lamp.Intensity = 5.0f;
        lamp.Range = 3.0f;
        base.GetScene().GetRegistry().Components.AddComponent(light, lamp);
        ASSERT_TRUE(base.GetScene().SetParent(light, post));
        ASSERT_TRUE(base.SaveAs((Root / "props/lamp_post.sscene").generic_string()));
        PostId = IdOf(base, post);
        LightId = IdOf(base, light);
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(Root, ec);
    }

    [[nodiscard]] static PersistentEntityId IdOf(const EditorDocument& doc, EntityId entity)
    {
        const auto* id = doc.GetRegistry().Components.TryGet<PersistentIdComponent>(entity);
        return id != nullptr ? id->Id : PersistentEntityId{};
    }

    [[nodiscard]] static EntityId FindById(const EditorDocument& doc, PersistentEntityId id)
    {
        for (EntityId entity : doc.GetScene().GetAllEntities())
            if (IdOf(doc, entity) == id)
                return entity;
        return {};
    }

    [[nodiscard]] EditorDocument Load(std::string_view text)
    {
        EditorDocument doc(Logging);
        doc.SetContentRoots({ Root });
        EXPECT_TRUE(doc.LoadFromSceneText(text));
        return doc;
    }

    [[nodiscard]] static PointLightComponent* Lamp(EditorDocument& doc, PersistentEntityId id)
    {
        return doc.GetScene().GetRegistry().Components.TryGet<PointLightComponent>(FindById(doc, id));
    }

    static constexpr std::string_view kDerived = "{ format_version: 2, extends: 'asset://props/lamp_post.sscene', entities: [] }";

    LoggingProvider Logging;
    fs::path Root;
    PersistentEntityId PostId;
    PersistentEntityId LightId;
};
} // namespace

TEST_F(PrefabExtendsProjectionTest, TheBasesEntitiesAreLiveUnderTheirOwnIds)
{
    EditorDocument derived = Load(kDerived);
    const EntityId light = FindById(derived, LightId);
    ASSERT_TRUE(light.IsValid());
    EXPECT_TRUE(derived.IsInherited(light));
    EXPECT_FALSE(derived.IsSceneInstanceMember(light)) << "not a placement member: no scene_instance on cook";
    EXPECT_EQ(derived.GetScene().GetParent(light), FindById(derived, PostId));
    ASSERT_NE(Lamp(derived, LightId), nullptr);
    EXPECT_EQ(Lamp(derived, LightId)->Range, 3.0f);
    EXPECT_TRUE(derived.GetProjectionDiagnostics().Clean());
}

TEST_F(PrefabExtendsProjectionTest, AnEditToAnInheritedEntityIsTheDocumentsSparsePatch)
{
    EditorDocument derived = Load(kDerived);
    Lamp(derived, LightId)->Intensity = 40.0f;
    const std::string saved = derived.ToSceneText();
    EXPECT_NE(saved.find("patch"), std::string::npos) << saved;
    EXPECT_NE(saved.find("intensity"), std::string::npos) << saved;
    EXPECT_EQ(saved.find("range"), std::string::npos) << "only what changed\n" << saved;
    EXPECT_EQ(saved.find(PersistentEntityIdToString(LightId) + "', components"), std::string::npos)
        << "an inherited entity is never written as a local\n" << saved;

    EditorDocument reopened = Load(saved);
    ASSERT_NE(Lamp(reopened, LightId), nullptr);
    EXPECT_EQ(Lamp(reopened, LightId)->Intensity, 40.0f);
    EXPECT_EQ(Lamp(reopened, LightId)->Range, 3.0f);
    EXPECT_EQ(reopened.ToSceneText(), saved) << "an editless save changes nothing";
}

TEST_F(PrefabExtendsProjectionTest, ABaseChangeReachesWhatTheChildDidNotTouch)
{
    EditorDocument derived = Load(kDerived);
    Lamp(derived, LightId)->Intensity = 40.0f;
    EditorDocument child = Load(derived.ToSceneText());

    {
        const fs::path basePath = Root / "props/lamp_post.sscene";
        std::ifstream in(basePath);
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const std::size_t at = text.find("range: 3");
        ASSERT_NE(at, std::string::npos) << text;
        text.replace(at, 8, "range: 9");
        std::ofstream(basePath, std::ios::trunc) << text;
    }
    const std::string saved[] = { std::string("asset://props/lamp_post.sscene") };
    ASSERT_TRUE(child.ReloadDependentSources(saved));
    ASSERT_NE(Lamp(child, LightId), nullptr);
    EXPECT_EQ(Lamp(child, LightId)->Range, 9.0f) << "the base's new value";
    EXPECT_EQ(Lamp(child, LightId)->Intensity, 40.0f) << "the child's own override";
}

TEST_F(PrefabExtendsProjectionTest, RemovingAnInheritedComponentIsWrittenAsARemoval)
{
    EditorDocument derived = Load(kDerived);
    derived.GetScene().GetRegistry().Components.RemoveComponent<PointLightComponent>(FindById(derived, LightId));
    const std::string saved = derived.ToSceneText();
    EXPECT_NE(saved.find("PointLight: null"), std::string::npos) << saved;

    EditorDocument reopened = Load(saved);
    EXPECT_TRUE(FindById(reopened, LightId).IsValid()) << "the entity stays; its component goes";
    EXPECT_EQ(Lamp(reopened, LightId), nullptr);
}

TEST_F(PrefabExtendsProjectionTest, ALocalBeneathAnInheritedEntityStaysALocal)
{
    EditorDocument derived = Load(kDerived);
    const EntityId added = derived.GetScene().CreateEntity(Vec3d{ 1, 0, 0 });
    ASSERT_TRUE(derived.GetScene().SetParent(added, FindById(derived, PostId)));
    const PersistentEntityId addedId = IdOf(derived, added);
    const std::string saved = derived.ToSceneText();
    EXPECT_EQ(saved.find("add_entities"), std::string::npos) << saved;

    EditorDocument reopened = Load(saved);
    const EntityId again = FindById(reopened, addedId);
    ASSERT_TRUE(again.IsValid());
    EXPECT_FALSE(reopened.IsInherited(again));
    EXPECT_EQ(reopened.GetScene().GetParent(again), FindById(reopened, PostId));
}

TEST_F(PrefabExtendsProjectionTest, DeletingAnInheritedEntityDeletesOnlyWhatThisDocumentAuthoredBeneathIt)
{
    EditorDocument derived = Load(kDerived);
    const EntityId post = FindById(derived, PostId);
    const EntityId added = derived.GetScene().CreateEntity(Vec3d{ 1, 0, 0 });
    ASSERT_TRUE(derived.GetScene().SetParent(added, post));
    const PersistentEntityId addedId = IdOf(derived, added);

    SelectionContext context;
    SelectionService selection(context);
    selection.BindDocument(&derived.GetScene().GetRegistry().Components);
    const EntityId doomed[] = { post };
    std::unique_ptr<ICommand> command = MakeDeleteEntitiesCommand(doomed, derived.GetScene(), derived, selection);
    ASSERT_NE(command, nullptr);
    command->Execute();
    EXPECT_TRUE(FindById(derived, PostId).IsValid()) << "the base's entity is the base's to remove";
    EXPECT_TRUE(FindById(derived, LightId).IsValid());
    EXPECT_FALSE(FindById(derived, addedId).IsValid()) << "what this document added beneath it goes";
}

// Kyusu harvests version 2.
TEST_F(PrefabExtendsProjectionTest, AnOpenedVersionOneLevelIsSavedAsVersionTwo)
{
    EditorDocument level = Load("{ format_version: 1, entities: [] }");
    const std::string saved = level.ToSceneText();
    EXPECT_NE(saved.find("format_version: 2"), std::string::npos) << saved;
}

TEST_F(PrefabExtendsProjectionTest, AChildIsRebuiltWhenItsMissingBaseAppears)
{
    EditorDocument derived(Logging);
    derived.SetContentRoots({ Root });
    (void)derived.LoadFromSceneText("{ format_version: 2, extends: 'asset://props/later.sscene', entities: [] }");
    EXPECT_FALSE(FindById(derived, PostId).IsValid());
    EXPECT_FALSE(derived.SourcesChangedOnDisk());

    fs::copy_file(Root / "props/lamp_post.sscene", Root / "props/later.sscene");
    EXPECT_TRUE(derived.SourcesChangedOnDisk());
    EXPECT_TRUE(derived.DependsOnSource("asset://props/later.sscene"));
    const std::vector<std::string> appeared{ "asset://props/later.sscene" };
    EXPECT_TRUE(derived.ReloadDependentSources(appeared));
    EXPECT_TRUE(FindById(derived, PostId).IsValid()) << "the base's entities arrive once it does";
}

TEST_F(PrefabExtendsProjectionTest, AnAbstractPrefabIsNeverPlaced)
{
    std::ofstream(Root / "props/glow.sscene")
        << "{ format_version: 2, abstract: true, entities: [ { id: '00000000000000f1', components: {} } ] }";
    EditorDocument level = Load("{ format_version: 2, entities: [] }");
    std::string error;
    EXPECT_FALSE(level.PlaceSceneInstance("asset://props/glow.sscene", Transform3f::Identity(), {}, &error).IsValid());
    EXPECT_NE(error.find("abstract"), std::string::npos) << error;
}

TEST_F(PrefabExtendsProjectionTest, AnAddARemovalAndAnEditOnAnInheritedEntityAreOnePatch)
{
    EditorDocument derived = Load(kDerived);
    World& world = derived.GetScene().GetRegistry().Components;
    const EntityId light = FindById(derived, LightId);
    world.RemoveComponent<PointLightComponent>(light);
    world.AddComponent(light, SpotLightComponent{});
    world.TryGet<LocalTransform>(light)->Value.Position.X = 4.0f;
    const std::string saved = derived.ToSceneText();
    EXPECT_EQ(saved.find("remove:"), std::string::npos) << saved;
    EXPECT_EQ(saved.find("add:"), std::string::npos) << saved;
    EXPECT_NE(saved.find("PointLight: null"), std::string::npos) << saved;
    EXPECT_NE(saved.find("SpotLight: {"), std::string::npos) << saved;

    EditorDocument reopened = Load(saved);
    const EntityId again = FindById(reopened, LightId);
    EXPECT_EQ(Lamp(reopened, LightId), nullptr);
    EXPECT_NE(reopened.GetScene().GetRegistry().Components.TryGet<SpotLightComponent>(again), nullptr);
    EXPECT_EQ(reopened.GetScene().GetRegistry().Components.TryGet<LocalTransform>(again)->Value.Position.X, 4.0f);
    EXPECT_EQ(reopened.ToSceneText(), saved);
}
