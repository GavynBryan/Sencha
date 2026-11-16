#include "problems/ProblemReveal.h"

#include "problems/ProblemList.h"
#include "workspaces/IWorkspace.h"

bool RevealProblemIn(std::span<IWorkspace* const> workspaces, const Problem& problem)
{
    const auto owns = [&](const IWorkspace& workspace) {
        return !problem.Document.has_value() || workspace.OwnsDocument(*problem.Document);
    };
    for (IWorkspace* workspace : workspaces)
        if (owns(*workspace) && workspace->RevealProblem(problem))
            return true;
    for (IWorkspace* workspace : workspaces)
        if (!owns(*workspace) && workspace->RevealProblem(problem))
            return true;
    return false;
}
