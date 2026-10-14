#include "problems/ProblemList.h"

#include <algorithm>

void ProblemList::Publish(std::string_view source, std::vector<Problem> problems)
{
    const std::string key(source);
    if (std::find(Order.begin(), Order.end(), key) == Order.end())
    {
        if (problems.empty())
            return;
        Order.push_back(key);
    }
    std::vector<Problem>& held = BySource[key];
    const bool unchanged = held.size() == problems.size()
        && std::equal(held.begin(), held.end(), problems.begin(), [](const Problem& a, const Problem& b) {
               return a.Severity == b.Severity && a.Message == b.Message && a.Location == b.Location
                   && a.Document == b.Document;
           });
    if (unchanged)
        return;
    held = std::move(problems);
    Observers.Notify();
}

std::vector<const Problem*> ProblemList::All() const
{
    std::vector<const Problem*> all;
    for (const std::string& source : Order)
    {
        const auto it = BySource.find(source);
        if (it == BySource.end())
            continue;
        for (const Problem& problem : it->second)
            all.push_back(&problem);
    }
    return all;
}

std::size_t ProblemList::Count(ProblemSeverity atLeast) const
{
    std::size_t count = 0;
    for (const auto& [source, problems] : BySource)
        for (const Problem& problem : problems)
            count += problem.Severity >= atLeast ? 1 : 0;
    return count;
}
