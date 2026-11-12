#include "WorkspaceFixture.h"

#include "authoring/PresentationProperties.h"
#include "workspace/PartitionOutline.h"
#include "workspace/ScenePresentationSource.h"

#include <gtest/gtest.h>

#include <string>

// Presentation settings are edited where they are authored -- a scene, a world,
// a graph -- as rows built from their schema, each showing what is in force and
// where it comes from, and every edit is one undoable step.
namespace
{
const PropertyNode* Find(const std::vector<PropertyNode>& nodes, std::string_view key)
{
    for (const PropertyNode& node : nodes)
    {
        if (node.Key == key)
            return &node;
        if (const PropertyNode* found = Find(node.Children, key))
            return found;
    }
    return nullptr;
}

PresentationSettingsLayer Bloom(bool enabled, float intensity)
{
    PresentationSettingsLayer layer;
    layer.Bloom.Enabled = enabled;
    layer.Bloom.Intensity = intensity;
    return layer;
}

class PresentationPropertiesTest : public WorkspaceTest
{
protected:
    PropertyEditResult Commit(IPropertySource& source, std::string_view key, std::string_view value)
    {
        return source.BeginEdit(key) ? source.CommitEdit(key, value) : PropertyEditResult::Refused;
    }

    void Pick(std::string_view key)
    {
        const std::optional<LevelLocation> location = ParseLevelLocation(key);
        ASSERT_TRUE(location.has_value()) << key;
        Outline.Choose(*location, ExplorerChoice::Pick);
    }

    [[nodiscard]] std::string KeyStartingWith(std::string_view prefix) const
    {
        for (const OutlineNode& node : Outline.OutlineNodes())
            if (node.Key.starts_with(prefix))
                return node.Key;
        return {};
    }

    PartitionOutline Outline{ Workspace };
    ScenePresentationSource SceneSettings{ Workspace };
};
} // namespace

TEST(PresentationProperties, ARowShowsWhatIsInForceAndWhichLayerSetsIt)
{
    const PresentationSettingsLayer game = Bloom(true, 0.5f);
    PresentationLayerStack below;
    below.Push({ .Level = PresentationLayerLevel::Game, .Name = {}, .Settings = &game });

    PresentationSettingsLayer scene;
    scene.Bloom.Threshold = 2.0f;
    const std::vector<PropertyNode> rows{ PresentationProperties(scene, below.Layers()) };

    const PropertyNode* intensity = Find(rows, "presentation.bloom.intensity");
    ASSERT_NE(intensity, nullptr);
    EXPECT_EQ(intensity->Value, "0.5");
    EXPECT_NE(intensity->Descriptor.Tooltip.find("from the game"), std::string::npos);

    const PropertyNode* threshold = Find(rows, "presentation.bloom.threshold");
    ASSERT_NE(threshold, nullptr);
    EXPECT_EQ(threshold->Value, "2");
    EXPECT_EQ(threshold->Descriptor.Tooltip.find("inherited"), std::string::npos);

    const PropertyNode* spread = Find(rows, "presentation.bloom.spread");
    ASSERT_NE(spread, nullptr);
    EXPECT_NE(spread->Descriptor.Tooltip.find("from the default"), std::string::npos);

    const PropertyNode* enabled = Find(rows, "presentation.bloom.enabled");
    ASSERT_NE(enabled, nullptr);
    EXPECT_EQ(enabled->Value, "Inherit");
}

TEST(PresentationProperties, AnEditSetsClearsOrRefusesOneField)
{
    const std::optional<PresentationSettingsLayer> set = EditPresentationLayer({}, "presentation.bloom.intensity", "0.8");
    ASSERT_TRUE(set.has_value());
    EXPECT_EQ(set->Bloom.Intensity, 0.8f);

    const std::optional<PresentationSettingsLayer> on = EditPresentationLayer(*set, "presentation.bloom.enabled", "On");
    ASSERT_TRUE(on.has_value());
    EXPECT_EQ(on->Bloom.Enabled, true);

    const std::optional<PresentationSettingsLayer> cleared =
        EditPresentationLayer(*on, "presentation.bloom.intensity", "");
    ASSERT_TRUE(cleared.has_value());
    EXPECT_FALSE(cleared->Bloom.Intensity.has_value());
    EXPECT_EQ(EditPresentationLayer(*cleared, "presentation.bloom.enabled", "Inherit"), PresentationSettingsLayer{});

    EXPECT_FALSE(EditPresentationLayer({}, "presentation.bloom.intensity", "-1").has_value());
    EXPECT_FALSE(EditPresentationLayer({}, "presentation.bloom.intensity", "bright").has_value());
    EXPECT_FALSE(IsPresentationKey("presentation.fog.density"));
    EXPECT_FALSE(IsPresentationKey("name"));
}

TEST_F(PresentationPropertiesTest, ASceneThatIsNoWorldEditsItsOwnLayerAsOneStep)
{
    Workspace.World.New();
    EXPECT_EQ(Commit(SceneSettings, "presentation.bloom.enabled", "On"), PropertyEditResult::Applied);
    EXPECT_EQ(Workspace.World.FocusDocument().GetPresentation().Bloom.Enabled, true);
    EXPECT_TRUE(Workspace.World.FocusDocument().IsDirty());
    Commands.Undo();
    EXPECT_TRUE(Workspace.World.FocusDocument().GetPresentation().IsEmpty());
}

TEST_F(PresentationPropertiesTest, TheWorldAGraphAndAZoneEachEditTheirOwnLayer)
{
    Pick("world");
    EXPECT_EQ(Commit(Outline, "presentation.bloom.enabled", "On"), PropertyEditResult::Applied);
    EXPECT_EQ(Workspace.World.Manifest().Presentation.Bloom.Enabled, true);

    Pick(KeyStartingWith("graph:"));
    EXPECT_EQ(Commit(Outline, "presentation.bloom.intensity", "0.4"), PropertyEditResult::Applied);
    EXPECT_EQ(Workspace.World.Manifest().Graphs.front().Presentation.Bloom.Intensity, 0.4f);
    const PropertyNode* enabled = Find(Outline.Nodes(), "presentation.bloom.enabled");
    ASSERT_NE(enabled, nullptr);
    EXPECT_NE(enabled->Descriptor.Tooltip.find("world"), std::string::npos) << enabled->Descriptor.Tooltip;

    Pick(KeyStartingWith("zone:"));
    const PropertyNode* intensity = Find(Outline.Nodes(), "presentation.bloom.intensity");
    ASSERT_NE(intensity, nullptr);
    EXPECT_EQ(intensity->Value, "0.4");
    EXPECT_NE(intensity->Descriptor.Tooltip.find("graph"), std::string::npos) << intensity->Descriptor.Tooltip;
    EXPECT_EQ(Commit(Outline, "presentation.bloom.intensity", "0.9"), PropertyEditResult::Applied);
    EXPECT_EQ(Workspace.World.FocusDocument().GetPresentation().Bloom.Intensity, 0.9f);

    Commands.Undo();
    EXPECT_TRUE(Workspace.World.FocusDocument().GetPresentation().IsEmpty());
    Commands.Undo();
    EXPECT_TRUE(Workspace.World.Manifest().Graphs.front().Presentation.IsEmpty());
    Commands.Undo();
    EXPECT_TRUE(Workspace.World.Manifest().Presentation.IsEmpty());
}
