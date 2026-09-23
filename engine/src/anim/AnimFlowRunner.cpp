#include <anim/AnimFlowRunner.h>

#include <anim/AnimContentSystem.h>
#include <anim/AnimFacts.h>

#include <algorithm>
#include <cmath>

namespace
{
    // Enough to catch a late joiner up through a flow of short sections, and
    // a bound on a flow whose sections are all shorter than a tick.
    constexpr int kMaxSectionsPerTick = 64;

    float ClipDuration(const AnimBoundRig& rig, std::uint16_t clip)
    {
        return clip < rig.Contents.size() ? rig.Contents[clip].DurationSeconds : 0.0f;
    }

    // The tick a section's clip ends: the first at or past its length, and
    // never the tick it began, so a flow always moves forward in time.
    AnimTick EndTick(AnimTick start, float duration, double tickSeconds)
    {
        const double ticks = tickSeconds > 0.0 ? std::ceil(static_cast<double>(duration) / tickSeconds - 1e-9) : 1.0;
        return start + std::max<AnimTick>(1, static_cast<AnimTick>(std::max(ticks, 1.0)));
    }

    struct Runner
    {
        const AnimFlowTick& Tick;
        AnimLayerContent& Layer;
        AnimLayerFlow& Flow;
        std::uint8_t LayerIndex = 0;
        AnimDecisionLog* Log = nullptr;
        AnimFlowOutcome Outcome;

        // The clip a section plays: its own, or its slot's current row,
        // resolved now and pinned for the section.
        std::uint16_t SectionClip(std::size_t index) const
        {
            const AnimBoundFlowSection& section = Tick.Flow->Sections[index];
            if (section.Content >= 0)
                return static_cast<std::uint16_t>(section.Content);
            const int row = ResolveAnimSlotRow(*Tick.Rig, section.Slot, *Tick.Inputs);
            if (row < 0)
                return kAnimNoContent;
            const int content = Tick.Rig->SlotRows[static_cast<std::size_t>(row)].Content;
            return content >= 0 && Tick.Rig->Contents[static_cast<std::size_t>(content)].Flow < 0
                ? static_cast<std::uint16_t>(content)
                : kAnimNoContent;
        }

        void Enter(std::size_t index, AnimTick start, AnimChangeReason reason)
        {
            const std::uint8_t previous = Flow.Phase == AnimFlowPhase::Playing ? Flow.Section : kAnimNoSection;
            Flow.LoopCount = reason == AnimChangeReason::SectionLooped ? static_cast<std::uint16_t>(Flow.LoopCount + 1) : 0;
            Flow.Section = static_cast<std::uint8_t>(index);
            Flow.SectionStartTick = start;
            Flow.Phase = AnimFlowPhase::Playing;
            Layer.Clip = SectionClip(index);
            Layer.ClipStartTick = start;
            Layer.ClipOffsetSeconds = 0.0f;
            if (reason != AnimChangeReason::SectionLooped)
                Outcome.SectionChanged = true;
            if (Log == nullptr)
                return;
            AnimDecisionRecord record;
            record.Tick = Tick.Now;
            record.Cause = AnimDecisionCause::SectionChanged;
            record.Reason = reason;
            record.Layer = LayerIndex;
            record.Behavior = Layer.Behavior;
            record.Row = Layer.Row;
            record.Content = Layer.Content;
            record.Section = Flow.Section;
            record.PreviousSection = previous;
            Log->Append(record);
        }

        void Complete()
        {
            Flow.Phase = AnimFlowPhase::Complete;
            Outcome.Complete = true;
        }

        // Whether the section just ended plays again.
        bool Loops(const AnimBoundFlowSection& section) const
        {
            switch (section.Loop)
            {
            case AnimFlowLoop::Once: return false;
            case AnimFlowLoop::While: return EvaluateAnimProgram(section.While, *Tick.Inputs).Passed;
            case AnimFlowLoop::Count:
            {
                if (Tick.Request == nullptr || section.CountParam < 0
                    || static_cast<std::size_t>(section.CountParam) >= kAnimRequestParams)
                    return false;
                const std::int32_t count = AnimFactToInt(Tick.Request->Params[section.CountParam]);
                return static_cast<std::int32_t>(Flow.LoopCount) + 1 < count;
            }
            }
            return false;
        }

        void Run()
        {
            const AnimBoundFlow& flow = *Tick.Flow;
            if (Tick.Entered || Flow.Phase == AnimFlowPhase::None)
            {
                Flow.Phase = AnimFlowPhase::None;
                // A request that carries the authority's anchor says where the
                // flow already is; otherwise it starts at the top, at the
                // request's own start when there is one.
                const AnimRequest* request = Tick.Request;
                if (request != nullptr && request->AnchorSection < flow.Sections.size()
                    && request->AnchorSectionStartTick <= Tick.Now)
                    Enter(request->AnchorSection, request->AnchorSectionStartTick, AnimChangeReason::FlowAnchored);
                else
                    Enter(0, Layer.StartTick, AnimChangeReason::FlowStarted);
            }

            for (int step = 0; step < kMaxSectionsPerTick && Flow.Phase == AnimFlowPhase::Playing; ++step)
            {
                const std::size_t current = Flow.Section;
                const AnimBoundFlowSection& section = flow.Sections[current];
                const bool inCancel = flow.Cancel >= 0 && current == static_cast<std::size_t>(flow.Cancel);

                if (Tick.Cancelling && !inCancel && section.CancelTiming == AnimCancelTiming::Immediate)
                {
                    if (flow.Cancel >= 0)
                    {
                        Enter(static_cast<std::size_t>(flow.Cancel), Tick.Now, AnimChangeReason::SectionCancelled);
                        continue;
                    }
                    Complete();
                    break;
                }

                const AnimTick end = EndTick(Flow.SectionStartTick, ClipDuration(*Tick.Rig, Layer.Clip), Tick.TickSeconds);
                if (Tick.Now < end)
                    break;

                // The section has ended; this decides what follows, once.
                if (inCancel)
                {
                    Complete();
                    break;
                }
                if (Tick.Cancelling)
                {
                    if (flow.Cancel >= 0)
                        Enter(static_cast<std::size_t>(flow.Cancel), end, AnimChangeReason::SectionCancelled);
                    else
                        Complete();
                    continue;
                }
                if (Loops(section))
                {
                    Enter(current, end, AnimChangeReason::SectionLooped);
                    continue;
                }
                const auto branch = std::ranges::find_if(section.Branches, [&](const AnimBoundFlowBranch& candidate) {
                    return EvaluateAnimProgram(candidate.When, *Tick.Inputs).Passed;
                });
                if (branch != section.Branches.end())
                {
                    Enter(branch->To, end, AnimChangeReason::SectionBranched);
                    continue;
                }
                if (section.Ends || current + 1 >= flow.Sections.size())
                {
                    Complete();
                    break;
                }
                Enter(current + 1, end, AnimChangeReason::SectionFollowed);
            }

            const float duration = ClipDuration(*Tick.Rig, Layer.Clip);
            const double elapsed = static_cast<double>(Tick.Now >= Flow.SectionStartTick ? Tick.Now - Flow.SectionStartTick : 0)
                * Tick.TickSeconds;
            Layer.TimeSeconds = Flow.Phase == AnimFlowPhase::Complete
                ? duration
                : static_cast<float>(std::min(elapsed, static_cast<double>(duration)));
            Outcome.Complete = Flow.Phase == AnimFlowPhase::Complete;
            Outcome.KeepTail = Tick.Request != nullptr && Tick.Request->IsCancelled() && !Outcome.Complete;
        }
    };
}

AnimFlowOutcome AdvanceAnimFlow(const AnimFlowTick& tick, AnimLayerContent& layer, AnimLayerFlow& flow,
                                std::uint8_t layerIndex, AnimDecisionLog* log)
{
    Runner runner{ tick, layer, flow, layerIndex, log, {} };
    runner.Run();
    return runner.Outcome;
}
