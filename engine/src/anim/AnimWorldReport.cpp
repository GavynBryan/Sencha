#include <anim/AnimWorldReport.h>

#include <anim/AnimRequestReport.h>
#include <anim/AnimRig.h>
#include <anim/AnimRigBinding.h>
#include <anim/AnimationRegistration.h>
#include <ecs/World.h>

#include <algorithm>
#include <map>

namespace
{
    template <typename... Components>
    std::uint32_t BytesOf(const World& world, EntityId entity, ComponentSet<Components...>*)
    {
        return (0u + ... + (world.IsRegistered<Components>() && world.TryGet<Components>(entity) != nullptr
                                ? static_cast<std::uint32_t>(sizeof(Components))
                                : 0u));
    }
}

std::uint32_t AnimEntityBytes(const World& world, EntityId entity)
{
    return BytesOf(world, entity, static_cast<AnimationComponents*>(nullptr));
}

std::vector<AnimRigWorldReport> ReportAnimWorld(World& world)
{
    AnimRigBindings* bindings = world.TryGetResource<AnimRigBindings>();
    if (bindings == nullptr || !world.IsRegistered<AnimRig>())
        return {};
    const World& reader = world;
    const bool hasReports = reader.IsRegistered<AnimRequestReport>();
    std::map<std::string, AnimRigWorldReport> byRig;
    AnimRigRunCache resolver(*bindings, world);
    reader.ForEachComponent<AnimRig>([&](EntityId entity, const AnimRig& rig) {
        const AnimBoundRig* bound = resolver.Resolve(rig.Rig);
        AnimRigWorldReport& report = byRig[bound != nullptr ? bound->RigPath : std::string()];
        report.RigPath = bound != nullptr ? bound->RigPath : std::string();
        const std::uint32_t bytes = AnimEntityBytes(reader, entity);
        report.MinEntityBytes = report.Entities == 0 ? bytes : std::min(report.MinEntityBytes, bytes);
        report.MaxEntityBytes = std::max(report.MaxEntityBytes, bytes);
        ++report.Entities;
        if (const AnimRequestReport* requests = hasReports ? reader.TryGet<AnimRequestReport>(entity) : nullptr)
        {
            report.UnplayedRequests += requests->Unplayed;
            report.OrphanedRequests += requests->Orphaned;
        }
    });
    std::vector<AnimRigWorldReport> reports;
    reports.reserve(byRig.size());
    for (auto& [path, report] : byRig)
        reports.push_back(std::move(report));
    return reports;
}
