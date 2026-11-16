#include "problems/ProblemList.h"
#include "problems/ProblemReveal.h"
#include "workspaces/IWorkspace.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace
{
class RevealingWorkspace final : public IWorkspace
{
public:
    RevealingWorkspace(std::string name, bool owns, bool reveals, std::vector<std::string>& asked)
        : Name(std::move(name)), Owns(owns), Reveals(reveals), Asked(asked)
    {
    }

    [[nodiscard]] bool OwnsDocument(const DocumentRef&) const override { return Owns; }
    bool RevealProblem(const Problem&) override
    {
        Asked.push_back(Name);
        return Reveals;
    }

private:
    std::string Name;
    bool Owns = false;
    bool Reveals = false;
    std::vector<std::string>& Asked;
};

const Problem kInDocument{ .Message = "edge reads a bool", .Location = "$.data.derived{Moving}.source.fact",
                           .Document = DocumentRef{ nullptr, "asset://animation/game.facts.sdata" } };
} // namespace

TEST(ProblemReveal, TheWorkspaceThatOwnsTheDocumentIsAskedFirst)
{
    std::vector<std::string> asked;
    RevealingWorkspace active("active", false, true, asked);
    RevealingWorkspace owner("owner", true, true, asked);
    IWorkspace* const workspaces[] = { &active, &owner };
    EXPECT_TRUE(RevealProblemIn(workspaces, kInDocument));
    EXPECT_EQ(asked, std::vector<std::string>{ "owner" });
}

TEST(ProblemReveal, ADocumentNobodyHasOpenIsShownByAWorkspaceThatCanOpenIt)
{
    std::vector<std::string> asked;
    RevealingWorkspace refuses("refuses", false, false, asked);
    RevealingWorkspace opens("opens", false, true, asked);
    IWorkspace* const workspaces[] = { &refuses, &opens };
    EXPECT_TRUE(RevealProblemIn(workspaces, kInDocument));
    EXPECT_EQ(asked, (std::vector<std::string>{ "refuses", "opens" }));
}

TEST(ProblemReveal, AProblemInNoDocumentGoesToTheFirstThatTakesIt)
{
    std::vector<std::string> asked;
    RevealingWorkspace first("first", false, true, asked);
    IWorkspace* const workspaces[] = { &first };
    EXPECT_TRUE(RevealProblemIn(workspaces, Problem{ .Message = "undeclared", .Location = "undeclared names" }));
    EXPECT_EQ(asked, std::vector<std::string>{ "first" });
}
