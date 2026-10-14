#include "shell/AssetTabs.h"
#include "workspaces/WorkspaceHost.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

// A window's rail has a tab per asset open in the editors placed in it, editor
// by editor; an editor with nothing open is a tab of its own, and the tab of the
// editor showing and the asset it shows is the current one.
namespace
{
class DocumentWorkspace final : public IWorkspace
{
public:
    explicit DocumentWorkspace(WorkspaceDocumentFacet facet) : Facet(std::move(facet)) {}
    [[nodiscard]] WorkspaceDocumentFacet Document() const override { return Facet; }
    WorkspaceDocumentFacet Facet;
};

constexpr PresentationId kMain{ 1, 1 };
constexpr PresentationId kSecond{ 2, 1 };

WorkspaceKind Kind(std::string id, std::string label, WorkspaceDocumentFacet facet)
{
    return WorkspaceKind{ .Id = id,
                          .DisplayName = std::move(label),
                          .RequiresProject = false,
                          .Create = [facet] { return std::make_unique<DocumentWorkspace>(facet); } };
}

std::vector<WorkspaceKind> Table()
{
    return {
        Kind("level", "Level", { .Label = "traversal3", .Changed = true }),
        Kind("materials", "Materials",
             { .Label = "brick", .Changed = false,
               .Open = { { .Key = "a", .Label = "concrete", .Changed = true }, { .Key = "b", .Label = "brick" } },
               .Active = "b" }),
        Kind("project", "Project", {}),
    };
}
} // namespace

TEST(AssetTabs, EachOpenAssetIsATabAndTheCurrentOneIsMarked)
{
    WorkspaceHost host(Table(), true, kMain);
    (void)host.Open("level");
    (void)host.Open("materials");

    const std::vector<AssetTab> tabs = AssetTabsIn(host, kMain);
    ASSERT_EQ(tabs.size(), 3u);
    EXPECT_EQ(tabs[0].Kind, "level");
    EXPECT_EQ(tabs[0].Label, "traversal3");
    EXPECT_TRUE(tabs[0].Document.empty()) << "an editor that names no assets is its own tab";
    EXPECT_TRUE(tabs[0].Changed);
    EXPECT_FALSE(tabs[0].Current);
    EXPECT_EQ(tabs[1].Label, "concrete");
    EXPECT_EQ(tabs[1].KindLabel, "Materials");
    EXPECT_TRUE(tabs[1].Changed);
    EXPECT_FALSE(tabs[1].Current);
    EXPECT_EQ(tabs[2].Document, "b");
    EXPECT_TRUE(tabs[2].Current) << "the asset the showing editor shows";
}

TEST(AssetTabs, AnEditorWithNothingOpenIsNamedForItselfAndOnlyItsOwnWindowShowsIt)
{
    WorkspaceHost host(Table(), true, kMain);
    (void)host.Open("project");
    (void)host.Open("level");
    ASSERT_TRUE(host.Place("project", kSecond));

    const std::vector<AssetTab> main = AssetTabsIn(host, kMain);
    ASSERT_EQ(main.size(), 1u);
    EXPECT_EQ(main[0].Kind, "level");

    const std::vector<AssetTab> second = AssetTabsIn(host, kSecond);
    ASSERT_EQ(second.size(), 1u);
    EXPECT_EQ(second[0].Label, "Project");
    EXPECT_TRUE(second[0].Current);
}
