#include "WorkspaceFixture.h"

#include "document/tools/BrushTool.h"
#include "document/tools/EdgeCutTool.h"
#include "tools/ToolRegistry.h"

// A tool's authoring settings live on the tool. That only works because tools
// outlive the document they are bound to, so these cover the pairing: the
// settings are reachable through the registry, and a document swap leaves them
// (and the tool holding them) intact.
namespace
{

class WorkspaceToolSettingsTest : public WorkspaceTest
{
protected:
    [[nodiscard]] ToolRegistry& Tools() { return *Workspace.Interaction.Tools; }

    template <typename T>
    [[nodiscard]] T& ToolById(std::string_view id)
    {
        for (const std::unique_ptr<ITool>& tool : Tools().GetTools())
            if (tool != nullptr && tool->GetId() == id)
                return static_cast<T&>(*tool);
        ADD_FAILURE() << "tool not registered: " << id;
        std::abort();
    }
};

TEST_F(WorkspaceToolSettingsTest, BrushCreationSettingsSurviveAFocusChange)
{
    const ZoneId second = AddSecondZone();
    ASSERT_TRUE(second.IsValid());

    BrushTool& brush = ToolById<BrushTool>("brush");
    brush.Creation.ActivePrimitive = BrushPrimitive::Cylinder;
    brush.Creation.CylinderSides = 24;
    brush.Creation.Inner = true;

    ASSERT_TRUE(Workspace.World.SetFocusZone(second));

    BrushTool& after = ToolById<BrushTool>("brush");
    EXPECT_EQ(after.Creation.ActivePrimitive, BrushPrimitive::Cylinder);
    EXPECT_EQ(after.Creation.CylinderSides, 24);
    EXPECT_TRUE(after.Creation.Inner);
}

TEST_F(WorkspaceToolSettingsTest, EdgeCutModeSurvivesAFocusChange)
{
    const ZoneId second = AddSecondZone();
    ASSERT_TRUE(second.IsValid());

    EdgeCutTool& cut = ToolById<EdgeCutTool>("edgecut");
    ASSERT_TRUE(cut.LoopCut); // the default
    cut.LoopCut = false;

    ASSERT_TRUE(Workspace.World.SetFocusZone(second));

    EXPECT_FALSE(ToolById<EdgeCutTool>("edgecut").LoopCut);
}

TEST_F(WorkspaceToolSettingsTest, EveryToolIsReachableAndSelfDescribing)
{
    // What the sidebar, status bar, and generated shortcuts each read. A tool
    // that answers these needs no registration anywhere else.
    for (const std::unique_ptr<ITool>& tool : Tools().GetTools())
    {
        ASSERT_NE(tool, nullptr);
        EXPECT_FALSE(tool->GetId().empty());
        EXPECT_FALSE(tool->GetDisplayName().empty());
    }
}

TEST_F(WorkspaceToolSettingsTest, ToolShortcutsAreDistinctAndClearOfTheFlyCamera)
{
    // The fly camera owns bare W/A/S/D and Q/E while it holds the pointer, and a
    // tool key that collided with those would fire mid-flight.
    static constexpr SDL_Keycode kCameraKeys[] = { SDLK_W, SDLK_A, SDLK_S,
                                                   SDLK_D, SDLK_Q, SDLK_E };
    std::vector<SDL_Keycode> seen;
    for (const std::unique_ptr<ITool>& tool : Tools().GetTools())
    {
        const ITool::Shortcut shortcut = tool->GetShortcut();
        if (shortcut.Key == SDLK_UNKNOWN)
            continue;
        const bool unmodified = !shortcut.Mods.Ctrl && !shortcut.Mods.Shift && !shortcut.Mods.Alt;
        if (unmodified)
            for (const SDL_Keycode camera : kCameraKeys)
                EXPECT_NE(shortcut.Key, camera) << "tool " << tool->GetId();
        EXPECT_EQ(std::find(seen.begin(), seen.end(), shortcut.Key), seen.end())
            << "duplicate tool shortcut on " << tool->GetId();
        seen.push_back(shortcut.Key);
    }
    EXPECT_FALSE(seen.empty());
}

TEST_F(WorkspaceToolSettingsTest, ActivatingAToolRunsItsEntrySetup)
{
    // Registration alone does not activate, so the first Activate is what runs
    // OnActivate; the brush tool's entry drops the element mode back to Object.
    Workspace.MeshEdit.SetElementKind(MeshElementKind::Face);
    ASSERT_TRUE(Tools().Activate("brush"));
    EXPECT_EQ(Workspace.MeshEdit.GetElementKind(), MeshElementKind::Object);
}

}
