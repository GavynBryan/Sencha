// A workspace's explorers as one table: a row is its action, its entry among
// the explorers and where paging and choosing in it go.

#include "actions/ActionCatalog.h"
#include "actions/ActionContext.h"
#include "workspaces/ExplorerTable.h"

#include <SDL3/SDL_keycode.h>

#include <gtest/gtest.h>

#include <string>
#include <vector>

TEST(ExplorerTable, ARowIsItsActionItsEntryAndWherePagingAndChoosingGo)
{
    ActionCatalog catalog;
    ActionContext context;
    ExplorerTable table;
    std::vector<std::string> chosen;
    for (const char* id : { "rigs", "joints" })
        table.Offer(catalog, context,
                    { .Id = id,
                      .Title = std::string(id) + " title",
                      .Chords = { { SDLK_R, {} } },
                      .Explore =
                          [id](const ExplorerQuery&) {
                              ExplorerPage page;
                              page.Empty = id;
                              return page;
                          },
                      .Choose = [&chosen, id](std::string_view key, ExplorerChoice, std::size_t) {
                          chosen.push_back(std::string(id) + ":" + std::string(key));
                      } });

    ASSERT_NE(catalog.Find("explorer.rigs"), nullptr);
    EXPECT_EQ(catalog.Find("explorer.rigs")->Label, "rigs title");
    EXPECT_EQ(catalog.Find("explorer.rigs")->Category, "Explore");
    const std::vector<WorkspaceExplorerInfo> infos = table.Infos();
    ASSERT_EQ(infos.size(), 2u);
    EXPECT_EQ(infos[1].Id, "joints");
    EXPECT_EQ(infos[1].Action, "explorer.joints") << "the entry and its action name each other once";
    EXPECT_EQ(infos[1].Shape, ExplorerShape::Outline);

    EXPECT_EQ(table.Explore({ .Explorer = "joints" }).Empty, "joints");
    EXPECT_TRUE(table.Explore({ .Explorer = "missing" }).Empty.empty());
    table.Choose("rigs", "hero", ExplorerChoice::Pick, 0);
    table.Choose("missing", "hero", ExplorerChoice::Pick, 0);
    EXPECT_EQ(chosen, std::vector<std::string>{ "rigs:hero" });
}
