#include "AnimRigBinder.h"

#include <anim/AnimationClipCache.h>
#include <authored/VerbBindingCompiler.h>
#include <authored/VerbBindingData.h>
#include <authored/WorldVocabulary.h>

#include <algorithm>
#include <format>
#include <tuple>

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

    // Lifecycle events: the one input they supply is the behavior's tag.
    for (AnimBoundBehavior& behavior : Out.Behaviors)
    {
        for (auto [decl, bound, field] :
             { std::tuple{ &behavior.Policy.OnEntered, &behavior.Entered, "on_entered" },
               std::tuple{ &behavior.Policy.OnExited, &behavior.Exited, "on_exited" } })
        {
            if (!decl->has_value())
                continue;
            AnimBoundEvent event;
            event.Scope = (*decl)->Scope;
            event.Binding = MakeVerbBindingKey((*decl)->Binding);
            event.BindingText = (*decl)->Binding;
            const CompiledVerbBinding* binding = Out.Bindings.Find(event.Binding);
            if (binding == nullptr)
            {
                Warning("anim.event.binding_unknown", behavior.DeclaredIn, field,
                        std::format("'{}' {} names binding '{}', which none of the rig's bindings declares.",
                                    behavior.Name, field, (*decl)->Binding));
                *bound = std::move(event);
                continue;
            }
            bool resolved = true;
            for (const VerbCompiledInput& input : binding->Inputs)
            {
                const VerbValue tag = VerbValue::Tag(behavior.Tag);
                const bool suits = input.Name == kAnimLifecycleBehaviorInput
                    && std::ranges::all_of(input.Destinations, [&tag](const VerbInputDestination& destination) {
                           return VerbValueSatisfiesField(tag, destination.Expected);
                       });
                if (!suits)
                {
                    Warning("anim.event.input_invalid", behavior.DeclaredIn, field,
                            std::format("Binding '{}' takes '{}', but a lifecycle event supplies only '{}', the "
                                        "behavior's tag, to a gameplay tag argument.",
                                        (*decl)->Binding, input.Name, kAnimLifecycleBehaviorInput));
                    resolved = false;
                    continue;
                }
                event.Inputs.push_back(tag);
            }
            event.Resolved = resolved;
            *bound = std::move(event);
        }
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
