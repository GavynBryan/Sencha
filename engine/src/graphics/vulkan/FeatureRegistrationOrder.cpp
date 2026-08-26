#include <graphics/vulkan/FeatureRegistrationOrder.h>

#include <algorithm>

namespace
{
// Whether `id` names something already committed in an earlier batch.
[[nodiscard]] bool IsRegistered(std::span<const std::string_view> registered,
                                std::string_view id)
{
    return std::find(registered.begin(), registered.end(), id) != registered.end();
}

// Index of `id` within `staged`, or npos.
[[nodiscard]] std::size_t IndexOf(std::span<const FeatureRegistration> staged,
                                  std::string_view id)
{
    for (std::size_t i = 0; i < staged.size(); ++i)
    {
        if (staged[i].Id == id)
            return i;
    }
    return static_cast<std::size_t>(-1);
}
} // namespace

std::string_view ToString(FeatureOrderFault fault)
{
    switch (fault)
    {
        case FeatureOrderFault::DuplicateId:      return "duplicate id";
        case FeatureOrderFault::UnknownDependency: return "unknown dependency";
        case FeatureOrderFault::Cycle:            return "dependency cycle";
    }
    return "unknown fault";
}

bool ResolveFeatureOrder(std::span<const FeatureRegistration> staged,
                         std::span<const std::string_view> registered,
                         std::vector<std::size_t>& order,
                         std::vector<FeatureOrderProblem>& problems)
{
    order.clear();
    problems.clear();

    for (std::size_t i = 0; i < staged.size(); ++i)
    {
        if (staged[i].Id.empty()
            || IsRegistered(registered, staged[i].Id)
            || IndexOf(staged, staged[i].Id) != i)
        {
            problems.push_back({ FeatureOrderFault::DuplicateId, staged[i].Id, {} });
        }
        for (const std::string_view dependency : staged[i].DependsOn)
        {
            if (IndexOf(staged, dependency) == static_cast<std::size_t>(-1)
                && !IsRegistered(registered, dependency))
            {
                problems.push_back(
                    { FeatureOrderFault::UnknownDependency, staged[i].Id, dependency });
            }
        }
    }
    if (!problems.empty())
        return false;

    // Stable topological order: repeatedly take the lowest-index feature whose
    // dependencies are all placed. Scanning in staging order is what makes ties
    // break by staging index rather than by whatever a container iterates in.
    std::vector<bool> placed(staged.size(), false);
    order.reserve(staged.size());
    while (order.size() < staged.size())
    {
        bool progressed = false;
        for (std::size_t i = 0; i < staged.size(); ++i)
        {
            if (placed[i])
                continue;
            const bool ready = std::all_of(
                staged[i].DependsOn.begin(), staged[i].DependsOn.end(),
                [&](std::string_view dependency)
                {
                    const std::size_t index = IndexOf(staged, dependency);
                    // A dependency committed in an earlier batch is already in
                    // place by construction.
                    return index == static_cast<std::size_t>(-1) || placed[index];
                });
            if (!ready)
                continue;
            order.push_back(i);
            placed[i] = true;
            progressed = true;
            break;
        }
        if (!progressed)
        {
            // Everything still unplaced is in a cycle or waiting behind one.
            // Reporting both together is honest: separating them needs a
            // strongly-connected-component pass and the outcome is the same,
            // since neither can be ordered.
            for (std::size_t i = 0; i < staged.size(); ++i)
            {
                if (!placed[i])
                    problems.push_back({ FeatureOrderFault::Cycle, staged[i].Id, {} });
            }
            order.clear();
            return false;
        }
    }
    return true;
}

std::string_view FindDependent(std::span<const FeatureRegistration> registered,
                               std::string_view id)
{
    for (const FeatureRegistration& feature : registered)
    {
        if (std::find(feature.DependsOn.begin(), feature.DependsOn.end(), id)
            != feature.DependsOn.end())
        {
            return feature.Id;
        }
    }
    return {};
}
