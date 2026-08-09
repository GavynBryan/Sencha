#include <movement/MovementProfileRuntime.h>

#include <gameplay_tags/GameplayTagRegistry.h>

#include <algorithm>
#include <format>
#include <utility>

namespace
{
    bool BindTagNames(const std::vector<std::string>& names,
                      const GameplayTagRegistry& registry,
                      std::vector<GameplayTagId>& output,
                      std::string& error)
    {
        for (const std::string& name : names)
        {
            GameplayTagId id = registry.FindTag(name);
            if (!id.IsValid())
            {
                error = std::format("unknown gameplay tag '{}'", name);
                return false;
            }
            output.push_back(id);
        }
        return true;
    }

    bool BindTagQuery(const std::vector<std::string>& all,
                      const std::vector<std::string>& any,
                      const std::vector<std::string>& none,
                      const GameplayTagRegistry& registry,
                      BoundMovementTagQuery& output,
                      std::string& error)
    {
        return BindTagNames(all, registry, output.All, error)
            && BindTagNames(any, registry, output.Any, error)
            && BindTagNames(none, registry, output.None, error);
    }

    bool BindLayer(const MovementProfileLayer& source,
                   const GameplayTagRegistry& tags,
                   const ResolveLocomotionModeFn& resolveMode,
                   BoundMovementLayer& output,
                   std::string& error)
    {
        output.Name = source.Name;
        output.When.Support = source.When.Support;
        output.When.ImmersionAtLeast = source.When.ImmersionAtLeast;
        output.Set = source.Set;
        output.Scale = source.Scale;
        output.Add = source.Add;
        output.SourcePath = source.SourcePath;

        if (source.When.Mode)
        {
            LocomotionModeId mode = resolveMode(*source.When.Mode);
            if (!mode.IsValid())
            {
                error = std::format("unknown locomotion mode '{}'", *source.When.Mode);
                return false;
            }
            output.When.Mode = mode;
        }

        output.When.Jump = source.When.Jump;
        return BindTagQuery(source.When.AllTags, source.When.AnyTags,
                            source.When.NoneTags, tags, output.When.Tags, error);
    }

    bool QueryMatches(const BoundMovementTagQuery& query,
                      const GameplayTagContainer* tags,
                      std::string* failure)
    {
        const auto has = [tags](GameplayTagId tag)
        {
            return tags != nullptr && tags->HasExact(tag);
        };

        for (GameplayTagId tag : query.All)
        {
            if (!has(tag))
            {
                if (failure) *failure = "required gameplay tag is absent";
                return false;
            }
        }
        if (!query.Any.empty())
        {
            const bool any = std::any_of(query.Any.begin(), query.Any.end(), has);
            if (!any)
            {
                if (failure) *failure = "none of the alternative gameplay tags are present";
                return false;
            }
        }
        for (GameplayTagId tag : query.None)
        {
            if (has(tag))
            {
                if (failure) *failure = "excluded gameplay tag is present";
                return false;
            }
        }
        return true;
    }

    bool LayerMatches(const BoundMovementLayer& layer,
                      const MovementResolveContext& context,
                      std::string* failure)
    {
        if (layer.When.Mode && *layer.When.Mode != context.Mode)
        {
            if (failure) *failure = "locomotion mode does not match";
            return false;
        }

        switch (layer.When.Support)
        {
        case MovementSupportCondition::Any:
            break;
        case MovementSupportCondition::None:
            if (context.Support != SupportKind::None)
            {
                if (failure) *failure = "support is not none";
                return false;
            }
            break;
        case MovementSupportCondition::Stable:
            if (context.Support != SupportKind::Stable)
            {
                if (failure) *failure = "support is not stable";
                return false;
            }
            break;
        case MovementSupportCondition::Steep:
            if (context.Support != SupportKind::Steep)
            {
                if (failure) *failure = "support is not steep";
                return false;
            }
            break;
        }

        if (layer.When.ImmersionAtLeast
            && context.Immersion < *layer.When.ImmersionAtLeast)
        {
            if (failure)
            {
                *failure = std::format("immersion {} is below {}",
                                       context.Immersion,
                                       *layer.When.ImmersionAtLeast);
            }
            return false;
        }

        if (layer.When.Jump && *layer.When.Jump != context.Jump)
        {
            if (failure)
            {
                *failure = context.Jump ? "this tick does not ask for a jump"
                                        : "this tick asks for a jump";
            }
            return false;
        }

        return QueryMatches(layer.When.Tags, context.Tags, failure);
    }

    void ApplyOptional(float& target,
                       const std::optional<float>& set,
                       const std::optional<float>& scale,
                       const std::optional<float>& add)
    {
        if (set) target = *set;
        if (scale) target *= *scale;
        if (add) target += *add;
    }

    void ApplyLayer(ResolvedMovementTuning& tuning, const BoundMovementLayer& layer)
    {
        for (const MovementTuningField& field : MovementTuningFields())
        {
            ApplyOptional(tuning.*field.Resolved,
                          layer.Set.*field.Patch,
                          layer.Scale.*field.Patch,
                          layer.Add.*field.Patch);
        }
    }

    void ResolveLayers(const std::vector<BoundMovementLayer>& layers,
                       const MovementResolveContext& context,
                       ResolvedMovementTuning& tuning,
                       std::vector<MovementLayerTrace>* trace)
    {
        for (const BoundMovementLayer& layer : layers)
        {
            std::string failure;
            const bool matched = LayerMatches(layer, context, trace ? &failure : nullptr);
            if (matched)
                ApplyLayer(tuning, layer);
            if (trace)
            {
                trace->push_back(MovementLayerTrace{
                    .Name = layer.Name,
                    .SourcePath = layer.SourcePath,
                    .Matched = matched,
                    .Failure = std::move(failure),
                    .After = tuning,
                });
            }
        }
    }

    const BoundMovementModeProfile* FindMode(const BoundMovementProfile& profile,
                                             LocomotionModeId mode)
    {
        const auto found = std::find_if(
            profile.Modes.begin(), profile.Modes.end(),
            [mode](const BoundMovementModeProfile& candidate)
            {
                return candidate.Mode == mode;
            });
        return found == profile.Modes.end() ? nullptr : &*found;
    }
}

std::span<const MovementTuningField> MovementTuningFields()
{
    static constexpr MovementTuningField kFields[] = {
        { "max_speed", &MovementTuningPatch::MaxSpeed, &ResolvedMovementTuning::MaxSpeed },
        { "acceleration", &MovementTuningPatch::Acceleration, &ResolvedMovementTuning::Acceleration },
        { "friction", &MovementTuningPatch::Friction, &ResolvedMovementTuning::Friction },
        { "stop_speed", &MovementTuningPatch::StopSpeed, &ResolvedMovementTuning::StopSpeed },
        { "wish_speed_cap", &MovementTuningPatch::WishSpeedCap, &ResolvedMovementTuning::WishSpeedCap },
        { "drag", &MovementTuningPatch::Drag, &ResolvedMovementTuning::Drag },
        { "gravity_scale", &MovementTuningPatch::GravityScale, &ResolvedMovementTuning::GravityScale },
        { "jump_speed", &MovementTuningPatch::JumpSpeed, &ResolvedMovementTuning::JumpSpeed },
    };
    return kFields;
}

MovementProfileBindResult BindMovementProfile(
    const CompiledMovementProfile& profile,
    const GameplayTagRegistry& tags,
    const ResolveLocomotionModeFn& resolveMode)
{
    MovementProfileBindResult result;
    BoundMovementProfile bound;

    bound.Layers.reserve(profile.Layers.size());
    for (const MovementProfileLayer& layer : profile.Layers)
    {
        BoundMovementLayer boundLayer;
        if (!BindLayer(layer, tags, resolveMode, boundLayer, result.Error))
            return result;
        bound.Layers.push_back(std::move(boundLayer));
    }

    bound.Modes.reserve(profile.Modes.size());
    for (const MovementModeProfile& modeData : profile.Modes)
    {
        const LocomotionModeId mode = resolveMode(modeData.Mode);
        if (!mode.IsValid())
        {
            result.Error = std::format("unknown locomotion mode '{}'", modeData.Mode);
            return result;
        }

        BoundMovementModeProfile boundMode;
        boundMode.Mode = mode;
        if (!BindTagQuery(modeData.SustainAllTags,
                          modeData.SustainAnyTags,
                          modeData.SustainNoneTags,
                          tags,
                          boundMode.Sustain,
                          result.Error))
        {
            return result;
        }

        boundMode.Layers.reserve(modeData.Layers.size());
        for (const MovementProfileLayer& layer : modeData.Layers)
        {
            BoundMovementLayer boundLayer;
            if (!BindLayer(layer, tags, resolveMode, boundLayer, result.Error))
                return result;
            boundMode.Layers.push_back(std::move(boundLayer));
        }
        bound.Modes.push_back(std::move(boundMode));
    }

    result.Profile = std::move(bound);
    return result;
}

MovementResolveResult ResolveMovementTuning(const BoundMovementProfile& profile,
                                            const MovementResolveContext& context,
                                            float moveSpeed,
                                            bool captureTrace)
{
    MovementResolveResult result;
    result.Tuning.MaxSpeed = moveSpeed;
    std::vector<MovementLayerTrace>* trace = captureTrace ? &result.Trace : nullptr;
    ResolveLayers(profile.Layers, context, result.Tuning, trace);

    if (const BoundMovementModeProfile* mode = FindMode(profile, context.Mode))
        ResolveLayers(mode->Layers, context, result.Tuning, trace);
    return result;
}

bool MovementModeSustainMatches(const BoundMovementProfile& profile,
                                LocomotionModeId mode,
                                const GameplayTagContainer& tags)
{
    const BoundMovementModeProfile* found = FindMode(profile, mode);
    return found == nullptr || QueryMatches(found->Sustain, &tags, nullptr);
}
