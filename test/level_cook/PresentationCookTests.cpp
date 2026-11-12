#include "document/DocumentCook.h"
#include "document/DocumentSerialization.h"
#include "document/EditorDocument.h"
#include "document/commands/ValueCommand.h"

#include "CookedSmapReaders.h"

#include <core/logging/LoggingProvider.h>
#include <render/PointLightComponent.h>
#include <world/scene/SmapFormat.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace
{
namespace fs = std::filesystem;

class PresentationCookTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite() { RegisterDocumentSerializers(); }

    void SetUp() override
    {
        Root = fs::temp_directory_path()
            / ("sencha_presentationcook_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        fs::remove_all(Root);
        fs::create_directories(Root);
        Write("materials/dev/gray.smat", "{}");
    }
    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(Root, ec);
    }

    void Write(const std::string& rel, std::string_view contents)
    {
        const fs::path path = Root / rel;
        fs::create_directories(path.parent_path());
        std::ofstream(path, std::ios::trunc) << contents;
    }

    [[nodiscard]] std::string Read(const fs::path& path) const
    {
        std::ifstream in(path);
        std::ostringstream text;
        text << in.rdbuf();
        return text.str();
    }

    fs::path Save(EditorDocument& doc, const std::string& rel)
    {
        const fs::path path = Root / rel;
        fs::create_directories(path.parent_path());
        EXPECT_TRUE(doc.SaveAs(path.generic_string()));
        return path;
    }

    // A light, so the scene has a runtime entity to cook.
    void AuthorLamp(EditorDocument& doc)
    {
        doc.SetContentRoots({ Root });
        const EntityId light = doc.GetScene().CreateEntity(Vec3d{ 0, 2, 0 });
        doc.GetScene().GetRegistry().Components.AddComponent(light, PointLightComponent{});
    }

    static PresentationSettingsLayer Bloom(float intensity)
    {
        PresentationSettingsLayer layer;
        layer.Bloom.Enabled = true;
        layer.Bloom.Intensity = intensity;
        return layer;
    }

    fs::path Root;
    LoggingProvider Logging;
};
} // namespace

TEST_F(PresentationCookTest, ASceneLayerIsAnUndoableEditThatSurvivesASave)
{
    EditorDocument doc(Logging);
    AuthorLamp(doc);
    const fs::path path = Save(doc, "levels/street.sscene");
    ASSERT_FALSE(doc.IsDirty());

    const std::unique_ptr<ICommand> edit = MakeSetPresentationCommand(doc.GetPresentation(), Bloom(0.8f), doc);
    edit->Execute();
    EXPECT_TRUE(doc.IsDirty());
    EXPECT_EQ(doc.GetPresentation(), Bloom(0.8f));
    edit->Undo();
    EXPECT_TRUE(doc.GetPresentation().IsEmpty());
    edit->Execute();
    ASSERT_TRUE(doc.Save());
    EXPECT_NE(Read(path).find("presentation"), std::string::npos);

    EditorDocument reloaded(Logging);
    reloaded.SetContentRoots({ Root });
    ASSERT_TRUE(reloaded.Load(path.generic_string()));
    EXPECT_EQ(reloaded.GetPresentation(), Bloom(0.8f));
    EXPECT_NE(Read(path).find("default_material"), std::string::npos);
}

TEST_F(PresentationCookTest, AnInvalidLayerRefusesTheLoadNamingIt)
{
    EditorDocument doc(Logging);
    doc.SetContentRoots({ Root });
    std::string error;
    EXPECT_FALSE(doc.LoadFromSceneText(
        R"({ "format_version": 1, "settings": { "presentation": { "bloom": { "intensity": -2 } } }, "entities": [] })",
        &error));
    EXPECT_NE(error.find("settings.presentation"), std::string::npos) << error;
}

TEST_F(PresentationCookTest, TheCookedSceneCarriesTheLayerAndAChangeDefeatsTheCache)
{
    EditorDocument doc(Logging);
    AuthorLamp(doc);
    doc.SetPresentation(Bloom(0.8f));
    const fs::path path = Save(doc, "levels/street.sscene");

    ASSERT_TRUE(CookDocument(path, Root, 16.0).Success);
    EXPECT_EQ(ReadCookedScene(Root / ".cooked/levels/street.smap").Presentation, Bloom(0.8f));
    EXPECT_TRUE(CookDocument(path, Root, 16.0).CacheHit);

    doc.SetPresentation(Bloom(0.3f));
    ASSERT_TRUE(doc.Save());
    const DocumentCookResult recook = CookDocument(path, Root, 16.0);
    ASSERT_TRUE(recook.Success) << recook.Error;
    EXPECT_FALSE(recook.CacheHit);
    EXPECT_EQ(ReadCookedScene(Root / ".cooked/levels/street.smap").Presentation, Bloom(0.3f));
}
