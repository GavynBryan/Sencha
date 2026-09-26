#include <anim/AnimationRegistration.h>

#include <anim/AnimContentSystem.h>
#include <anim/AnimDecisionLog.h>
#include <anim/AnimEventSystem.h>
#include <anim/AnimFactGatherSystem.h>
#include <anim/AnimFactProviders.h>
#include <anim/AnimPoseSystem.h>
#include <anim/AnimRequestJournal.h>
#include <anim/AnimRootMotionSource.h>
#include <anim/AnimRig.h>
#include <anim/AnimRigBinding.h>
#include <anim/AnimRigRisk.h>
#include <anim/AnimSelectSystem.h>
#include <anim/AnimTrace.h>
#include <anim/AnimWorldReport.h>
#include <app/EngineSchedule.h>
#include <core/console/ConsoleRegistry.h>
#include <core/json/JsonFormat.h>
#include <ecs/World.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <movement/RootMotionSource.h>
#include <world/ComponentRegistrar.h>

#include <algorithm>
#include <charconv>
#include <format>
#include <fstream>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

void RegisterAnimationComponents(ComponentRegistrar& registrar)
{
    registrar.AddAll<AnimationComponents>();
}

void InstallAnimationVocabulary(World& world)
{
    if (GameplayTagRegistry* tags = world.TryGetResource<GameplayTagRegistry>())
    {
        for (const char* layer : { "anim.layer.base", "anim.layer.upper", "anim.layer.aim" })
            (void)tags->RegisterTag(layer);
    }
    if (!world.HasResource<AnimFactProviders>())
        world.AddResource<AnimFactProviders>();
    if (!world.HasResource<AnimRigLimits>())
        world.AddResource<AnimRigLimits>();
    if (!world.HasResource<AnimRequestJournal>())
        world.AddResource<AnimRequestJournal>();
    // Movement asks this what carries a character; animation answers from the
    // clip its base layer plays.
    if (!world.HasResource<RootMotionSource>())
        world.AddResource<RootMotionSource>(RootMotionSource{ &SampleAnimRootMotion });
}

namespace
{
    // "index:generation", as anim.trace lists it, naming a live entity with a rig.
    std::optional<EntityId> ParseAnimatedEntity(const World& world, std::string_view text)
    {
        const std::size_t colon = text.find(':');
        if (colon == std::string_view::npos)
            return std::nullopt;
        EntityId entity;
        const auto parse = [](std::string_view part, std::uint32_t& out) {
            const auto [end, error] = std::from_chars(part.data(), part.data() + part.size(), out);
            return error == std::errc{} && end == part.data() + part.size();
        };
        if (!parse(text.substr(0, colon), entity.Index) || !parse(text.substr(colon + 1), entity.Generation))
            return std::nullopt;
        if (!world.IsRegistered<AnimRig>() || world.TryGet<AnimRig>(entity) == nullptr)
            return std::nullopt;
        return entity;
    }
}

void RegisterAnimationConsole(ConsoleRegistry& console, World& world)
{
    const AnimRigLimits* limits = world.TryGetResource<AnimRigLimits>();
    const std::int64_t cap = limits != nullptr ? limits->BlendOverrideCap : AnimRigLimits{}.BlendOverrideCap;
    (void)console.RegisterCVar({
        .Name = "anim.blend.override_cap",
        .Owner = "engine",
        .Type = CVarType::Int,
        .DefaultValue = cap,
        .CurrentValue = cap,
        .Flags = CVarFlags::None,
        .Help = "How many pairwise blend overrides one rig may bind. Rigs past half of it warn; past it "
                "they fail to bind. Changing it rebinds every rig.",
        .Source = { "engine" },
        .Min = 0.0,
        .Max = 4096.0,
        .OnChange = [&world](const CVarChangeContext& ctx) {
            if (AnimRigLimits* live = world.TryGetResource<AnimRigLimits>())
                live->BlendOverrideCap = static_cast<std::uint32_t>(std::get<std::int64_t>(ctx.NewValue));
        },
    });

    (void)console.RegisterCommand({
        .Name = "anim.trace",
        .Owner = "engine",
        .Usage = "anim.trace [entity]",
        .Help = "Lists the animated entities as index:generation, or starts recording one's decisions.",
        .Callback = [&world](ConsoleExecutionContext&, std::span<const std::string> args) {
            ConsoleResult result;
            if (!world.IsRegistered<AnimRig>())
            {
                result.Info("no animated entities");
                return result;
            }
            if (args.empty())
            {
                const World& reader = world;
                AnimRigBindings* bindings = world.TryGetResource<AnimRigBindings>();
                reader.ForEachComponent<AnimRig>([&](EntityId entity, const AnimRig& rig) {
                    const AnimBoundRig* bound = bindings != nullptr ? bindings->Resolve(rig.Rig, world) : nullptr;
                    const bool tracing = reader.IsRegistered<AnimDecisionLog>()
                                      && reader.TryGet<AnimDecisionLog>(entity) != nullptr;
                    result.Info(std::format("{}:{} {}{}", entity.Index, entity.Generation,
                                            bound != nullptr ? bound->RigPath : std::string("(unbound)"),
                                            tracing ? " (recording)" : ""));
                });
                return result;
            }
            const std::optional<EntityId> entity = ParseAnimatedEntity(world, args[0]);
            if (!entity)
            {
                result.Status = ConsoleStatus::InvalidArguments;
                result.Error(std::format("'{}' is not an animated entity; `anim.trace` lists them", args[0]));
                return result;
            }
            if (std::as_const(world).TryGet<AnimDecisionLog>(*entity) == nullptr)
                world.AddComponent(*entity, AnimDecisionLog{});
            result.Info(std::format("recording {}; `anim.trace.export {} <file>` writes what it has", args[0],
                                    args[0]));
            return result;
        },
    });
    (void)console.RegisterCommand({
        .Name = "anim.trace.export",
        .Owner = "engine",
        .Usage = "anim.trace.export <entity> <file>",
        .Help = "Writes an entity's recorded decisions, with names resolved, as an animation.trace document.",
        .Callback = [&world](ConsoleExecutionContext&, std::span<const std::string> args) {
            ConsoleResult result;
            const std::optional<EntityId> entity = args.size() == 2 ? ParseAnimatedEntity(world, args[0]) : std::nullopt;
            if (!entity)
            {
                result.Status = ConsoleStatus::InvalidArguments;
                result.Error("usage: anim.trace.export <entity> <file>");
                return result;
            }
            AnimRigBindings* bindings = world.TryGetResource<AnimRigBindings>();
            const AnimBoundRig* rig =
                bindings != nullptr ? bindings->Resolve(std::as_const(world).TryGet<AnimRig>(*entity)->Rig, world)
                                    : nullptr;
            const JsonValue trace = WriteAnimTrace(world, *entity, rig);
            if (trace.IsNull())
            {
                result.Status = ConsoleStatus::ExecutionFailed;
                result.Error(std::format("{} is not recording; `anim.trace {}` starts it", args[0], args[0]));
                return result;
            }
            std::ofstream out(args[1], std::ios::binary);
            out << JsonFormat(trace, 2) << "\n";
            if (!out.good())
            {
                result.Status = ConsoleStatus::ExecutionFailed;
                result.Error(std::format("could not write {}", args[1]));
                return result;
            }
            result.Info(std::format("wrote {}", args[1]));
            return result;
        },
    });
    (void)console.RegisterCommand({
        .Name = "anim.risk",
        .Owner = "engine",
        .Usage = "anim.risk",
        .Help = "Every bound rig's content risk -- blend overrides, selector rules, long flows, what drifts "
                "toward a graph -- and what its entities carry and have left unplayed.",
        .Callback = [&world](ConsoleExecutionContext&, std::span<const std::string>) {
            ConsoleResult result;
            const AnimRigBindings* bindings = world.TryGetResource<AnimRigBindings>();
            const GameplayTagRegistry* tags = world.TryGetResource<GameplayTagRegistry>();
            if (bindings == nullptr)
            {
                result.Info("no rigs bound");
                return result;
            }
            std::vector<const AnimBoundRig*> rigs;
            bindings->ForEachBound([&](const AnimBoundRig& rig) { rigs.push_back(&rig); });
            std::ranges::sort(rigs, {}, &AnimBoundRig::RigPath);
            const std::vector<AnimRigWorldReport> reports = ReportAnimWorld(world);
            for (const AnimBoundRig* bound : rigs)
            {
                const AnimBoundRig& rig = *bound;
                const AnimRigRisk risk = MeasureAnimRigRisk(rig, tags);
                result.Info(std::format("{}: {} overrides, {} rules (deepest {}), {} long flows", rig.RigPath,
                                        risk.BlendOverrides, risk.SelectorRules, risk.DeepestSelector,
                                        risk.LongFlows));
                const auto report = std::ranges::find(reports, rig.RigPath, &AnimRigWorldReport::RigPath);
                if (report != reports.end())
                    result.Info(std::format("  {} entities at {} bytes each; {} requests went unplayed",
                                            report->Entities,
                                            report->MinEntityBytes == report->MaxEntityBytes
                                                ? std::format("{}", report->MaxEntityBytes)
                                                : std::format("{}-{}", report->MinEntityBytes, report->MaxEntityBytes),
                                            report->UnplayedRequests));
                for (const AnimRigRiskFinding& finding : risk.Findings)
                    result.Info(std::format("  [{}] {}", finding.Rule, finding.Message));
            }
            return result;
        },
    });
}

void RegisterAnimationSystems(EngineSchedule& schedule, LoggingProvider* logging, const AnimationHost& host)
{
    schedule.Register<AnimFactGatherSystem>(logging, host.PresentsPose);
    schedule.Register<AnimSelectSystem>(host.PresentsPose);
    schedule.Register<AnimContentSystem>(host.PresentsPose);
    AnimEventSystem& eventSystem = schedule.Register<AnimEventSystem>(host.Verbs, host.PresentsPose);
    // Selection reads this tick's facts; resolution reads this tick's winners;
    // events cross the content time resolution just advanced.
    schedule.After<AnimSelectSystem, AnimFactGatherSystem>();
    schedule.After<AnimContentSystem, AnimSelectSystem>();
    schedule.After<AnimEventSystem, AnimContentSystem>();
    // Root motion reads what content resolution decided. Declared only when movement
    // is registered, since a schedule with no characters has nothing to carry.
    if (schedule.Get<RootMotionSystem>() != nullptr)
        schedule.After<RootMotionSystem, AnimContentSystem>();

    // Posing runs in the post-fixed phase, after movement has moved what it
    // poses; nothing in the fixed phase reads it.
    if (host.PresentsPose)
        schedule.Register<AnimPoseSystem>(host.Jobs);

    if (host.Console != nullptr)
    {
        (void)host.Console->RegisterCVar({
            .Name = "anim.events.queue_capacity",
            .Owner = "engine",
            .Type = CVarType::Int,
            .DefaultValue = static_cast<std::int64_t>(eventSystem.GetCapacity()),
            .CurrentValue = static_cast<std::int64_t>(eventSystem.GetCapacity()),
            .Flags = CVarFlags::None,
            .Help = "How many clip events one tick may offer to their bindings. A crossing past it is "
                    "recorded as refused rather than dropped unseen.",
            .Source = { "engine" },
            .Min = 1.0,
            .Max = 65536.0,
            .OnChange = [&eventSystem](const CVarChangeContext& ctx) {
                eventSystem.SetCapacity(static_cast<std::size_t>(std::get<std::int64_t>(ctx.NewValue)));
            },
        });
    }
}
