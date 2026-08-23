#include <render/ProjectedShadowCVars.h>

#include <core/console/ConsoleRegistry.h>

#include <algorithm>

namespace
{

void RegisterDouble(ConsoleRegistry& registry, const char* name, double defaultValue,
                    const char* help, std::optional<double> min = {},
                    std::optional<double> max = {})
{
    registry.RegisterCVar({
        .Name = name,
        .Owner = "engine",
        .Type = CVarType::Double,
        .DefaultValue = defaultValue,
        .CurrentValue = defaultValue,
        .Flags = CVarFlags::Archive,
        .Help = help,
        .Source = { "renderer defaults" },
        .Min = min,
        .Max = max,
    });
}

float ReadDouble(const ConsoleRegistry* registry, std::string_view name, float fallback)
{
    if (registry == nullptr)
        return fallback;
    const CVarMetadata* metadata = registry->FindCVar(name);
    if (metadata == nullptr)
        return fallback;
    const double* value = std::get_if<double>(&metadata->CurrentValue);
    return value != nullptr ? static_cast<float>(*value) : fallback;
}

} // namespace

void RegisterProjectedShadowCVars(ConsoleRegistry& registry)
{
    registry.RegisterCVar({
        .Name = "render.shadow.projected",
        .Owner = "engine",
        .Type = CVarType::Bool,
        .DefaultValue = true,
        .CurrentValue = true,
        .Flags = CVarFlags::Archive,
        .Help = "Grounds moving objects with crisp projected silhouette "
                "shadows (the cheap technique; light shadow maps are "
                "unaffected).",
        .Source = { "renderer defaults" },
    });

    RegisterDouble(registry, "render.shadow.projected.darkness", 0.55,
                   "How dark a projected object shadow multiplies its "
                   "receiver, 0 = invisible, 1 = black.", 0.0, 1.0);
    RegisterDouble(registry, "render.shadow.projected.max_distance", 6.0,
                   "How far a projected object shadow reaches along its "
                   "direction, in world units.", 0.5, 50.0);
    RegisterDouble(registry, "render.shadow.projected.fade_start", 0.35,
                   "Where along the projection depth the shadow starts "
                   "fading, as a fraction of its reach.", 0.0, 1.0);
    RegisterDouble(registry, "render.shadow.projected.smoothing", 16.0,
                   "Per-second convergence rate of a caster's shadow "
                   "direction toward its lights.", 0.5, 60.0);
    RegisterDouble(registry, "render.shadow.projected.min_pitch", 20.0,
                   "Minimum downward pitch of a derived shadow direction, "
                   "degrees below horizontal. Keeps grounding shadows "
                   "grounding when a light sits below a caster's center.",
                   0.0, 89.0);
    RegisterDouble(registry, "render.shadow.projected.dir_x", 0.0,
                   "Fallback shadow direction X, used when no light dominates "
                   "a caster. Interim source: an authored environment record "
                   "replaces it.");
    RegisterDouble(registry, "render.shadow.projected.dir_y", -1.0,
                   "Fallback shadow direction Y.");
    RegisterDouble(registry, "render.shadow.projected.dir_z", 0.0,
                   "Fallback shadow direction Z.");
    RegisterDouble(registry, "render.shadow.projected.tile_px", 256.0,
                   "Silhouette tile edge in pixels.", 32.0, 512.0);
    RegisterDouble(registry, "render.shadow.projected.bias", 0.03,
                   "Occlusion depth bias in world units. Fragments within "
                   "this distance of the nearest receiver along the shadow "
                   "ray still receive; walls thinner than about twice this "
                   "leak.", 0.005, 0.5);
    RegisterDouble(registry, "render.shadow.projected.softness", 3.0,
                   "Silhouette blur reach in atlas texels; 0 is sharp "
                   "coverage, higher is a wider, softer penumbra.", 0.0, 8.0);
    RegisterDouble(registry, "render.shadow.projected.max_casters", 16.0,
                   "Projected-shadow casters per frame; the farthest drop "
                   "first and the drop is counted.", 1.0, 64.0);
    RegisterDouble(registry, "render.shadow.projected.max_receivers", 24.0,
                   "Receiver re-draws per caster.", 1.0, 256.0);
}

ProjectedShadowBudgets ReadProjectedShadowBudgets(const ConsoleRegistry* registry)
{
    ProjectedShadowBudgets budgets;
    budgets.TilePixels = static_cast<std::uint32_t>(std::clamp(
        ReadDouble(registry, "render.shadow.projected.tile_px",
                   static_cast<float>(budgets.TilePixels)), 32.0f, 512.0f));
    budgets.MaxCasters = static_cast<std::uint32_t>(std::clamp(
        ReadDouble(registry, "render.shadow.projected.max_casters",
                   static_cast<float>(budgets.MaxCasters)), 1.0f, 64.0f));
    budgets.MaxReceiversPerCaster = static_cast<std::uint32_t>(std::clamp(
        ReadDouble(registry, "render.shadow.projected.max_receivers",
                   static_cast<float>(budgets.MaxReceiversPerCaster)), 1.0f, 256.0f));
    budgets.BiasWorld = std::clamp(
        ReadDouble(registry, "render.shadow.projected.bias",
                   budgets.BiasWorld), 0.005f, 0.5f);
    budgets.SoftnessTexels = std::clamp(
        ReadDouble(registry, "render.shadow.projected.softness",
                   budgets.SoftnessTexels), 0.0f, 8.0f);
    budgets.MaxDistance = std::clamp(
        ReadDouble(registry, "render.shadow.projected.max_distance",
                   budgets.MaxDistance),
        0.5f, 50.0f);
    return budgets;
}
