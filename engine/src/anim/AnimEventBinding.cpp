#include "AnimRigBinder.h"

#include <anim/AnimationClipCache.h>
#include <authored/VerbBindingCompiler.h>
#include <authored/VerbBindingData.h>
#include <authored/WorldVocabulary.h>

#include <algorithm>
#include <format>

void AnimRigBinder::BindEvents(const AnimRigData& rig)
{
    VerbBindingEnvironment environment = MakeVerbBindingEnvironment(WorldRef);
    environment.DataAssets = &Data;

    // The rig's binding files, compiled into one set. A record that fails to
    // compile is the binding file's problem: its events stay unresolved and
    // the rig still animates.
    for (std::size_t b = 0; b < rig.BindingSetPaths.size(); ++b)
    {
        const std::string& path = rig.BindingSetPaths[b];
        const std::string field = std::format("$.data.bindings[{}]", b);
        if (Load<VerbBindingLibrary>(path, kVerbBindingsTypeName, Out.RigPath, field) == nullptr)
            continue;
        if (environment.Verbs == nullptr)
        {
            Warning("anim.event.no_catalog", Out.RigPath, field,
                    "This World declares no authored vocabulary, so none of the rig's bindings can resolve.");
            continue;
        }
        std::vector<std::string> errors;
        const DataAssetHandle handle = Data.Find(path);
        if (b == 0)
            Out.Bindings.InstantiateFrom(Data, handle, environment, errors);
        else
            Out.Bindings.AppendFrom(Data, handle, environment, errors);
        for (std::string& error : errors)
            Warning("anim.event.binding_invalid", path, {}, std::move(error));
    }

    // A lifecycle event supplies one input, a tag: the behavior's or the
    // section's. A binding that takes anything else cannot be satisfied.
    const auto bindLifecycle = [&](const AnimLifecycleDecl& decl, std::string_view inputName, GameplayTagId tag,
                                   const std::string& asset, const std::string& field,
                                   const std::string& owner) {
        AnimBoundEvent event;
        event.Scope = decl.Scope;
        event.Binding = MakeVerbBindingKey(decl.Binding);
        event.BindingText = decl.Binding;
        const CompiledVerbBinding* binding = Out.Bindings.Find(event.Binding);
        if (binding == nullptr)
        {
            Warning("anim.event.binding_unknown", asset, field,
                    std::format("{} {} names binding '{}', which none of the rig's bindings declares.", owner, field,
                                decl.Binding));
            return event;
        }
        bool resolved = true;
        const VerbValue value = VerbValue::Tag(tag);
        for (const VerbCompiledInput& input : binding->Inputs)
        {
            const bool suits = input.Name == inputName
                && std::ranges::all_of(input.Destinations, [&value](const VerbInputDestination& destination) {
                       return VerbValueSatisfiesField(value, destination.Expected);
                   });
            if (!suits)
            {
                Warning("anim.event.input_invalid", asset, field,
                        std::format("Binding '{}' takes '{}', but a lifecycle event supplies only '{}', a tag, to a "
                                    "gameplay tag argument.",
                                    decl.Binding, input.Name, inputName));
                resolved = false;
                continue;
            }
            event.Inputs.push_back(value);
        }
        event.Resolved = resolved;
        return event;
    };

    for (AnimBoundBehavior& behavior : Out.Behaviors)
    {
        if (behavior.Policy.OnEntered)
            behavior.Entered = bindLifecycle(*behavior.Policy.OnEntered, kAnimLifecycleBehaviorInput, behavior.Tag,
                                             behavior.DeclaredIn, "on_entered", "'" + behavior.Name + "'");
        if (behavior.Policy.OnExited)
            behavior.Exited = bindLifecycle(*behavior.Policy.OnExited, kAnimLifecycleBehaviorInput, behavior.Tag,
                                            behavior.DeclaredIn, "on_exited", "'" + behavior.Name + "'");
    }
    for (AnimBoundFlow& flow : Out.Flows)
        for (AnimBoundFlowSection& section : flow.Sections)
        {
            if (flow.SectionEnteredDecl)
                section.Entered = bindLifecycle(*flow.SectionEnteredDecl, kAnimSectionLifecycleInput, section.Tag,
                                                flow.Path, "on_section_entered", "Section '" + section.TagName + "'");
            if (flow.SectionExitedDecl)
                section.Exited = bindLifecycle(*flow.SectionExitedDecl, kAnimSectionLifecycleInput, section.Tag,
                                               flow.Path, "on_section_exited", "Section '" + section.TagName + "'");
        }

    for (AnimBoundContent& content : Out.Contents)
    {
        const AnimationClipData* clip = Clips != nullptr ? Clips->Get(content.Clip) : nullptr;
        if (clip == nullptr)
            continue;
        for (std::size_t e = 0; e < clip->Events.size(); ++e)
        {
            const AnimationClipEvent& event = clip->Events[e];
            const std::string field = std::format("$.events[{}]", e);
            AnimBoundEvent bound;
            bound.Key = event.Key;
            bound.Time = event.Time;
            bound.Scope = event.Scope;
            bound.MinWeight = event.MinWeight;
            bound.Binding = MakeVerbBindingKey(event.Binding);
            bound.BindingText = event.Binding;

            const CompiledVerbBinding* binding = Out.Bindings.Find(bound.Binding);
            if (binding == nullptr)
            {
                Warning("anim.event.binding_unknown", content.Path, field,
                        std::format("Event {} names binding '{}', which none of the rig's bindings declares.",
                                    event.Key, event.Binding));
                content.Events.push_back(std::move(bound));
                continue;
            }

            // One value per binding input, in the binding's order, converted
            // now so a crossing only copies them.
            bool resolved = true;
            bound.Inputs.resize(binding->Inputs.size());
            for (std::size_t i = 0; i < binding->Inputs.size(); ++i)
            {
                const VerbCompiledInput& input = binding->Inputs[i];
                const auto supplied = std::ranges::find_if(
                    event.Inputs, [&input](const VerbBindingArgument& value) { return value.Key == input.Name; });
                if (supplied == event.Inputs.end())
                {
                    Warning("anim.event.input_missing", content.Path, field,
                            std::format("Event {} supplies no '{}', which binding '{}' takes.", event.Key,
                                        input.Name, event.Binding));
                    resolved = false;
                    continue;
                }
                std::vector<std::string> errors;
                if (!CompileVerbInputValue(*supplied, input, environment, event.Binding, bound.Inputs[i], errors))
                {
                    for (std::string& error : errors)
                        Warning("anim.event.input_invalid", content.Path, field,
                                std::format("Event {}: {}", event.Key, error));
                    resolved = false;
                }
            }
            for (const VerbBindingArgument& supplied : event.Inputs)
            {
                const bool used = std::ranges::any_of(
                    binding->Inputs, [&supplied](const VerbCompiledInput& input) { return input.Name == supplied.Key; });
                if (!used)
                {
                    Warning("anim.event.input_unused", content.Path, field,
                            std::format("Event {} supplies '{}', which binding '{}' does not take.", event.Key,
                                        supplied.Key, event.Binding));
                    resolved = false;
                }
            }
            bound.Resolved = resolved;
            content.Events.push_back(std::move(bound));
        }
    }
}
