#include <app/TransformPropagationPass.h>

#include <world/registry/Registry.h>

#include <unordered_set>

void TransformPropagationPass::PostFixed(PostFixedContext& ctx)
{
    Propagate(ctx.ActiveRegistries);
}

void TransformPropagationPass::ExtractRender(RenderExtractContext& ctx)
{
    Propagate(ctx.ActiveRegistries);
}

void TransformPropagationPass::Propagate(std::span<Registry*> registries)
{
    std::unordered_set<Registry*> seen;
    for (Registry* registry : registries)
    {
        if (registry == nullptr || !seen.insert(registry).second)
            continue;

        auto* transforms = registry->Components.TryGet<TransformStore<Transform3f>>();
        auto* hierarchy = registry->Resources.TryGet<TransformHierarchyService>();
        auto* order = registry->Resources.TryGet<TransformPropagationOrderService>();

        if (transforms == nullptr || hierarchy == nullptr || order == nullptr)
            continue;

        TransformPropagationSystem<Transform3f> propagation(*transforms, *hierarchy, *order);
        propagation.Propagate();
    }
}
