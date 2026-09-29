#include <anim/AnimRigTimingIdentity.h>

#include <anim/AnimRigBinding.h>
#include <anim/AnimationClipCache.h>
#include <gameplay_tags/GameplayTagRegistry.h>

#include <cstring>
#include <string_view>

namespace
{
    // FNV-1a, kept here rather than on core/hash/Fnv1a.h: machines compare this
    // identity, so it owns its construction, as the cooked scene format does.
    struct Hasher
    {
        const AnimBoundRig& Rig;
        const GameplayTagRegistry* Tags;
        std::uint64_t Value = 14695981039346656037ull;

        void Bytes(const void* data, std::size_t size)
        {
            const auto* bytes = static_cast<const unsigned char*>(data);
            for (std::size_t i = 0; i < size; ++i)
            {
                Value ^= bytes[i];
                Value *= 1099511628211ull;
            }
        }

        template <typename T>
        void Plain(T value)
        {
            Bytes(&value, sizeof(value));
        }

        // Length first, so "ab" + "c" and "a" + "bc" differ.
        void Text(std::string_view text)
        {
            Plain(static_cast<std::uint64_t>(text.size()));
            Bytes(text.data(), text.size());
        }

        void Tag(GameplayTagId tag)
        {
            if (!tag.IsValid())
                Text({});
            else if (Tags != nullptr)
                Text(Tags->GetName(tag));
            else
                Plain(tag.Value);
        }

        void Slot(std::uint32_t slot) { Text(slot < Rig.Slots.size() ? std::string_view(Rig.Slots[slot].Name) : "?"); }

        void Program(const AnimProgram& program)
        {
            Plain(static_cast<std::uint64_t>(program.Ops.size()));
            for (const AnimOpcode& op : program.Ops)
            {
                Plain(op.Op);
                Plain(op.Mode);
                Plain(op.Index);
                Plain(op.Constant);
                switch (op.Op)
                {
                case AnimOp::PushSlot: Slot(op.Operand); break;
                case AnimOp::PushReqAge:
                case AnimOp::PushReqParam:
                case AnimOp::PushReqCancelReason:
                case AnimOp::ReqActive: Tag(GameplayTagId{ op.Operand }); break;
                case AnimOp::TagQuery:
                    if (op.Operand < program.Queries.size())
                        for (const GameplayTagId tag : program.Queries[op.Operand])
                            Tag(tag);
                    break;
                default: Plain(op.Operand); break;
                }
            }
            for (const std::uint32_t end : program.RowEnds)
                Plain(end);
        }

        // Only events that decide something: a cosmetic one may differ.
        void Event(const std::optional<AnimBoundEvent>& event)
        {
            Plain(event.has_value() && event->Scope == AnimEventScope::Gameplay);
            if (event.has_value() && event->Scope == AnimEventScope::Gameplay)
                Text(event->BindingText);
        }
    };
}

std::uint64_t AnimRigTimingIdentity(const AnimBoundRig& rig, const AnimationClipCache* clips,
                                    const GameplayTagRegistry* tags)
{
    Hasher h{ rig, tags };

    for (const AnimBoundFactSlot& slot : rig.Slots)
    {
        h.Text(slot.Name);
        h.Plain(slot.Kind);
        h.Plain(slot.Local);
        h.Plain(slot.Derivation);
    }
    for (const AnimBoundDerivation& derivation : rig.Derivations)
    {
        h.Plain(derivation.Op);
        h.Slot(derivation.Result);
        for (std::uint8_t s = 0; s < derivation.SourceCount; ++s)
        {
            h.Slot(derivation.Sources[s].Slot);
            h.Plain(derivation.Sources[s].Negate);
        }
        h.Plain(derivation.SourceKind);
        h.Plain(derivation.Rising);
        h.Plain(derivation.MatchValue);
        h.Plain(derivation.Compare);
        h.Plain(derivation.WindowMs);
        h.Plain(derivation.Enter);
        h.Plain(derivation.Exit);
        h.Plain(derivation.Constant);
    }

    for (const AnimBoundIntent& intent : rig.Intents)
    {
        h.Text(intent.Name);
        for (const AnimBoundParam& param : intent.Params)
        {
            h.Text(param.Name);
            h.Plain(param.Kind);
        }
    }

    for (const AnimBoundLayer& layer : rig.Layers)
    {
        h.Text(layer.NameText);
        h.Plain(layer.Selector);
        h.Tag(layer.Idle);
    }

    for (const AnimBoundBehavior& behavior : rig.Behaviors)
    {
        h.Text(behavior.Name);
        h.Plain(behavior.Policy.Kind);
        h.Plain(behavior.Policy.Latch.Mode);
        h.Plain(behavior.Policy.Latch.InterruptibleBy);
        h.Plain(behavior.Policy.Latch.Priority);
        for (const std::string& tag : behavior.Policy.Latch.Tags)
            h.Text(tag);
        h.Plain(behavior.Policy.Latch.OnInterrupt);
        h.Plain(behavior.Policy.Latch.OnRequestCancel);
        h.Plain(behavior.Policy.LateJoin);
        h.Plain(behavior.Policy.Rate);
        h.Plain(behavior.Policy.StartSeconds);
        h.Plain(behavior.Policy.RootMotion);
        h.Event(behavior.Entered);
        h.Event(behavior.Exited);
    }

    for (const AnimBoundSelector& selector : rig.Selectors)
    {
        for (const AnimBoundRule& rule : selector.Rules)
        {
            h.Plain(rule.PriorityBand);
            h.Program(rule.Enter);
            h.Plain(rule.HasStay);
            if (rule.HasStay)
                h.Program(rule.Stay);
            h.Tag(rule.Behavior);
            h.Plain(rule.HoldMinMs);
            h.Plain(rule.CooldownMs);
            h.Plain(rule.CooldownSlot);
            h.Tag(rule.LatchIntent);
        }
    }

    for (const AnimBoundSlotRow& row : rig.SlotRows)
    {
        h.Text(row.BehaviorName);
        h.Plain(row.Priority);
        h.Program(row.When);
        h.Plain(row.Content);
    }

    for (const AnimBoundContent& content : rig.Contents)
    {
        h.Plain(content.DurationSeconds);
        // What carries a character is timing two machines must agree on.
        const AnimationClipData* clip = clips != nullptr && content.Clip.IsValid() ? clips->Get(content.Clip) : nullptr;
        const bool carries = clip != nullptr && clip->Root.has_value();
        h.Plain(carries);
        if (carries)
        {
            h.Bytes(clip->Root->TimesSeconds.data(), clip->Root->TimesSeconds.size() * sizeof(float));
            h.Bytes(clip->Root->Values.data(), clip->Root->Values.size() * sizeof(float));
        }
        h.Plain(content.Flow);
        h.Plain(content.Blendspace);
        for (const AnimBoundEvent& event : content.Events)
        {
            if (event.Scope != AnimEventScope::Gameplay)
                continue;
            h.Plain(event.Time);
            h.Text(event.BindingText);
        }
    }

    for (const AnimBoundBlendspace& blendspace : rig.Blendspaces)
    {
        h.Plain(blendspace.AxisCount);
        for (std::uint8_t a = 0; a < blendspace.AxisCount && a < 2; ++a)
        {
            h.Text(blendspace.Axes[a].Fact);
            h.Plain(blendspace.Axes[a].Min);
            h.Plain(blendspace.Axes[a].Max);
        }
        for (const AnimBoundBlendspaceSample& sample : blendspace.Samples)
        {
            h.Plain(sample.Content);
            h.Plain(sample.At[0]);
            h.Plain(sample.At[1]);
        }
    }

    for (const AnimBoundFlow& flow : rig.Flows)
    {
        h.Plain(flow.Cancel);
        for (const AnimBoundFlowSection& section : flow.Sections)
        {
            h.Text(section.TagName);
            h.Plain(section.Content);
            h.Tag(section.Slot);
            h.Plain(section.Loop);
            h.Program(section.While);
            h.Tag(section.CountIntent);
            h.Plain(section.CountParam);
            for (const AnimBoundFlowBranch& branch : section.Branches)
            {
                h.Program(branch.When);
                h.Plain(branch.To);
            }
            h.Plain(section.Ends);
            h.Plain(section.CancelTiming);
            h.Event(section.Entered);
            h.Event(section.Exited);
        }
    }

    // Zero is what an unstamped request set holds.
    return h.Value == 0 ? 1 : h.Value;
}
