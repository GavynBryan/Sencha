#include <navigation/NavRoutePlanning.h>

#include <jobs/JobSystem.h>
#include <navigation/NavigationQuery.h>
#include <navigation/ZoneNavigation.h>

#include <algorithm>

NavPlanOutput PlanRoute(const NavPlanInput& input, NavQueryContext& context, NavRouteBuffer& route)
{
    route.Clear();
    NavPlanOutput output;
    if (input.Zone == nullptr)
    {
        output.Status = NavStatus::ZoneUnavailable;
        return output;
    }
    NavQueryRequest request;
    request.Zone = input.Zone->Zone();
    request.Profile = input.Profile;
    request.Policy = input.Policy;
    request.Capabilities = std::span(input.Capabilities.data(), input.CapabilityCount);
    request.AvoidLinks = std::span(input.AvoidLinks.data(), input.AvoidLinkCount);

    const NavProjectResult start = NavProjectPoint(*input.Zone, context, request, input.From, input.Extents);
    if (start.Status != NavStatus::Success)
    {
        output.Status = start.Status;
        return output;
    }
    const NavProjectResult end = NavProjectPoint(*input.Zone, context, request, input.To, input.Extents);
    if (end.Status != NavStatus::Success)
    {
        output.Status = end.Status == NavStatus::InvalidStart ? NavStatus::InvalidDestination : end.Status;
        return output;
    }
    output.Start = start.Location;
    output.End = end.Location;
    output.Status = NavFindRoute(*input.Zone, context, request, start.Location, end.Location, route,
                                 &output.Diagnostics);
    return output;
}

NavRoutePlanner::NavRoutePlanner(JobSystem* jobs, NavQueryContextConfig context, NavRouteCapacity capacity)
    : Jobs(jobs)
    , ContextConfig(context)
    , Capacity(capacity)
{
}

NavRoutePlanner::~NavRoutePlanner() = default;

void NavRoutePlanner::Plan(std::span<const NavPlanInput> inputs, std::size_t minParallel)
{
    const bool parallel = Jobs != nullptr && Jobs->WorkerCount() > 0 && inputs.size() >= minParallel
        && inputs.size() > 1;
    const std::size_t contexts = parallel ? Jobs->WorkerCount() + 1 : 1;
    while (Contexts.size() < contexts)
        Contexts.emplace_back(ContextConfig);
    while (Routes.size() < inputs.size())
        Routes.push_back(std::make_unique<NavRouteBuffer>(Capacity.Steps, Capacity.Corners, Capacity.Dependencies));
    Outputs.resize(std::max(Outputs.size(), inputs.size()));

    if (!parallel)
    {
        for (std::size_t i = 0; i < inputs.size(); ++i)
            Outputs[i] = PlanRoute(inputs[i], Contexts[0], *Routes[i]);
        return;
    }
    Jobs->ParallelFor(static_cast<std::uint32_t>(inputs.size()), [&](std::uint32_t i) {
        Outputs[i] = PlanRoute(inputs[i], Contexts[Jobs->CurrentWorkerIndex()], *Routes[i]);
    });
}
