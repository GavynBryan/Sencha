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

    struct Runner
    {
        const AnimFlowAdvanceInput& Tick;
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
            return content >= 0 && Tick.Rig->Contents[static_cast<std::size_t>(content)].IsClip()
                ? static_cast<std::uint16_t>(content)
                : kAnimNoContent;
        }

        void Enter(std::size_t index, AnimTick start, AnimChangeReason reason)
        {
            const std::uint8_t previous = Flow.Phase == AnimFlowPhase::Playing ? Flow.Section : kAnimNoSection;
            Flow.LoopCount = reason == AnimChangeReason::SectionLooped ? static_cast<std::uint16_t>(Flow.LoopCount + 1) : 0;
            Flow.Section = static_cast<std::uint8_t>(index);
            Flow.SectionTag = Tick.Flow->Sections[index].Tag;
            Flow.SectionStartTick = start;
            if (reason != AnimChangeReason::SectionLooped)
                Flow.SectionEnteredTick = start;
            Flow.Phase = AnimFlowPhase::Playing;
            Layer.Clip = SectionClip(index);
            // A section plays its clip once, forward; its loops are new starts.
            Layer.Playback = AnimPlayback{ .StartTick = start, .OffsetSeconds = 0.0f, .Rate = 1.0f,
                                           .DurationSeconds = ClipDuration(*Tick.Rig, Layer.Clip), .Cyclic = false };
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

        // After a rebind the playing section follows its tag; a flow whose section is
        // gone starts again, and says so.
        void Relocate()
        {
            const AnimBoundFlow& flow = *Tick.Flow;
            const auto found = std::ranges::find(flow.Sections, Flow.SectionTag, &AnimBoundFlowSection::Tag);
            if (found == flow.Sections.end())
            {
                Flow.Phase = AnimFlowPhase::None;
                if (Log != nullptr)
                {
                    AnimDecisionRecord record;
                    record.Tick = Tick.Now;
                    record.Cause = AnimDecisionCause::IndexReset;
                    record.Reason = AnimChangeReason::Rebound;
                    record.Layer = LayerIndex;
                    record.Behavior = Layer.Behavior;
                    record.Section = Flow.Section;
                    Log->Append(record);
                }
                return;
            }
            const auto index = static_cast<std::size_t>(found - flow.Sections.begin());
            Flow.Section = static_cast<std::uint8_t>(index);
            // Same place in the section; its clip and length are the new binding's.
            Layer.Clip = SectionClip(index);
            Layer.Playback.DurationSeconds = ClipDuration(*Tick.Rig, Layer.Clip);
        }

        void Run()
        {
            const AnimBoundFlow& flow = *Tick.Flow;
            if (Tick.Rebound && !Tick.Entered && Flow.Phase != AnimFlowPhase::None)
                Relocate();
            if (Tick.Entered || Flow.Phase == AnimFlowPhase::None)
            {
                Flow.Phase = AnimFlowPhase::None;
                // An anchored request says where the flow already is; otherwise the flow starts at
                // the top, at the request's own start when there is one.
                const AnimRequest* request = Tick.Request;
                if (request != nullptr && request->AnchorSection < flow.Sections.size()
                    && request->AnchorSectionStartTick <= Tick.Now)
                    Enter(request->AnchorSection, request->AnchorSectionStartTick, AnimChangeReason::FlowAnchored);
                else
                    Enter(0, Layer.StartTick, AnimChangeReason::FlowStarted);
            }
            else if (Tick.FollowsAnchor && Flow.Phase == AnimFlowPhase::Playing && Tick.Request != nullptr
                     && Tick.Request->AnchorSection < flow.Sections.size()
                     && Tick.Request->AnchorSectionStartTick <= Tick.Now)
            {
                // The authority's anchor arrives a flight late. It corrects this machine when it
                // names the playing section with another entry, or another section entered no
                // earlier; an older stamp for another section is already stale here.
                const AnimRequest& anchor = *Tick.Request;
                const bool sameSection = anchor.AnchorSection == Flow.Section;
                const bool moved = sameSection ? anchor.AnchorSectionStartTick != Flow.SectionEnteredTick
                                               : anchor.AnchorSectionStartTick >= Flow.SectionEnteredTick;
                if (moved)
                    Enter(anchor.AnchorSection, anchor.AnchorSectionStartTick, AnimChangeReason::FlowAnchored);
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

                const AnimTick end = AnimPlaybackEndTick(Layer.Playback, Tick.TickSeconds);
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

            Layer.TimeSeconds = Flow.Phase == AnimFlowPhase::Complete
                ? Layer.Playback.DurationSeconds
                : static_cast<float>(AnimPlaybackSeconds(Layer.Playback, Tick.Now, Tick.TickSeconds));
            Outcome.Complete = Flow.Phase == AnimFlowPhase::Complete;
            Outcome.KeepTail = Tick.Request != nullptr && Tick.Request->IsCancelled() && !Outcome.Complete;
        }
    };
}

AnimFlowOutcome AdvanceAnimFlow(const AnimFlowAdvanceInput& tick, AnimLayerContent& layer, AnimLayerFlow& flow,
                                std::uint8_t layerIndex, AnimDecisionLog* log)
{
    Runner runner{ tick, layer, flow, layerIndex, log, {} };
    runner.Run();
    return runner.Outcome;
}
