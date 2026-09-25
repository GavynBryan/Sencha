#include <anim/AnimTrace.h>

#include <anim/AnimBehaviorSet.h>
#include <anim/AnimDecisionLog.h>
#include <anim/AnimRig.h>
#include <anim/AnimRigBinding.h>
#include <authored/VerbInvocation.h>
#include <ecs/World.h>
#include <gameplay_tags/GameplayTagRegistry.h>

#include <format>

namespace
{
    JsonValue Text(std::string_view text) { return JsonValue(std::string(text)); }
    JsonValue Number(double value) { return JsonValue(value); }
}

JsonValue WriteAnimTrace(const World& world, EntityId entity, const AnimBoundRig* rig)
{
    if (!world.IsRegistered<AnimDecisionLog>())
        return JsonValue();
    const AnimDecisionLog* log = world.TryGet<AnimDecisionLog>(entity);
    if (log == nullptr)
        return JsonValue();
    const GameplayTagRegistry* tags = world.TryGetResource<GameplayTagRegistry>();
    const auto name = [&](GameplayTagId tag) {
        return tags != nullptr && tag.IsValid() ? JsonValue(std::string(tags->GetName(tag))) : JsonValue();
    };

    JsonValue::Array records;
    for (std::size_t i = 0; i < log->Size(); ++i)
    {
        const AnimDecisionRecord& record = log->At(i);
        JsonValue::Object out{ { "tick", Number(static_cast<double>(record.Tick)) },
                               { "cause", Text(AnimDecisionCauseName(record.Cause)) } };
        if (record.Reason != AnimChangeReason::None)
            out.emplace_back("reason", Text(AnimChangeReasonName(record.Reason)));
        if (record.Layer != kAnimNoLayer)
        {
            out.emplace_back("layer", Number(record.Layer));
            if (rig != nullptr && record.Layer < rig->Layers.size())
                out.emplace_back("layer_name", Text(rig->Layers[record.Layer].NameText));
        }
        if (record.Behavior.IsValid())
            out.emplace_back("behavior", name(record.Behavior));
        if (record.PreviousBehavior.IsValid())
            out.emplace_back("previous_behavior", name(record.PreviousBehavior));
        if (record.Intent.IsValid())
            out.emplace_back("intent", name(record.Intent));
        if (record.Request.IsValid())
            out.emplace_back("request", Number(record.Request.Sequence));
        if (record.Rule != kAnimNoRule)
            out.emplace_back("rule", Number(record.Rule));
        if (record.PreviousRule != kAnimNoRule)
            out.emplace_back("previous_rule", Number(record.PreviousRule));
        if (record.Content != kAnimNoContent)
            out.emplace_back("content", rig != nullptr && record.Content < rig->Contents.size()
                                            ? Text(rig->Contents[record.Content].Path)
                                            : Number(record.Content));
        if (record.Section != 0xFF)
            out.emplace_back("section", Number(record.Section));
        if (record.CancelReason != AnimCancelReason::None)
            out.emplace_back("cancel", Text(AnimCancelReasonName(record.CancelReason)));
        if (record.RejectReason != AnimRejectReason::None)
            out.emplace_back("reject", Text(AnimRejectReasonName(record.RejectReason)));
        if (record.Cause == AnimDecisionCause::EventCrossed || record.Cause == AnimDecisionCause::BehaviorEntered
            || record.Cause == AnimDecisionCause::BehaviorExited || record.Cause == AnimDecisionCause::SectionEntered
            || record.Cause == AnimDecisionCause::SectionExited)
        {
            out.emplace_back("event", Number(record.EventKey));
            out.emplace_back("outcome", Text(AnimEventOutcomeName(record.EventOutcome)));
            out.emplace_back("admission", Text(VerbAdmissionName(record.Admission)));
        }
        if (record.Cause == AnimDecisionCause::BlendApplied)
            out.emplace_back("blend", JsonValue(JsonValue::Object{
                                          { "mode", Text(AnimBlendModeName(record.Blend)) },
                                          { "seconds", Number(record.BlendSeconds) },
                                          { "magnitude", Number(record.BlendMagnitude) },
                                          { "overridden", JsonValue(record.BlendOverridden) } }));
        records.emplace_back(std::move(out));
    }

    JsonValue::Object trace{ { "type", Text(kAnimTraceType) },
                             { "version", Number(1.0) },
                             { "entity", Text(std::format("{}:{}", entity.Index, entity.Generation)) } };
    if (rig != nullptr)
        trace.emplace_back("rig", Text(rig->RigPath));
    // Said outright, so a reader never mistakes a gap for a pose held still.
    trace.emplace_back("captured", Text("decisions"));
    trace.emplace_back("records_written", Number(static_cast<double>(log->Written)));
    trace.emplace_back("records", JsonValue(std::move(records)));
    return JsonValue(std::move(trace));
}
