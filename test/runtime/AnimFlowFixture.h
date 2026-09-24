#pragma once

// A request-keyed flow on one prop: the reload intent plays a pump-action
// flow (open, a counted insert loop, close) under its behavior's latch policy.
// Shared by the flow tests and the paired authority/client tests, which need
// the same content on two machines.

#include "AnimRigFixture.h"

#include <anim/AnimFacts.h>
#include <anim/AnimFlowState.h>

#include <format>
#include <string>
#include <string_view>
#include <vector>

inline constexpr const char* kFlowTags[] = {
    "Anim.Idle", "anim.intent.reload", "Anim.Reload", "Anim.Hit", "Anim.Reload.Shell",
    "Anim.Reload.Open", "Anim.Reload.Insert", "Anim.Reload.Close",
};

// Open for 15 ticks, insert for 30 a request's `shells` times, close for
// 15; a cancel goes to the close.
inline constexpr std::string_view kPumpFlow = R"({ "sections": [
    { "tag": "Anim.Reload.Open", "clip": "asset://anim/open.sanim" },
    { "tag": "Anim.Reload.Insert", "clip": "asset://anim/insert.sanim", "loop": "count",
      "count_intent": "anim.intent.reload", "count_param": "shells" CANCEL_TIMING },
    { "tag": "Anim.Reload.Close", "clip": "asset://anim/close.sanim" } ],
    "cancel": "Anim.Reload.Close" })";

inline std::string PumpFlow(std::string_view insertCancelTiming = {})
{
    std::string flow(kPumpFlow);
    const std::string timing =
        insertCancelTiming.empty() ? std::string() : std::format(R"(, "cancel_timing": "{}")", insertCancelTiming);
    flow.replace(flow.find("CANCEL_TIMING"), std::string_view("CANCEL_TIMING").size(), timing);
    return flow;
}

inline void LoadFlowCommon(AnimRigFixture& fx)
{
    for (const char* tag : kFlowTags)
        (void)fx.Tags().RegisterTag(tag);
    fx.Clip("asset://anim/idle.sanim", 1.0f);
    fx.Clip("asset://anim/open.sanim", 0.25f);
    fx.Clip("asset://anim/insert.sanim", 0.5f);
    fx.Clip("asset://anim/insert_crouch.sanim", 0.5f);
    fx.Clip("asset://anim/close.sanim", 0.25f);
    fx.Clip("asset://anim/hit.sanim", 0.5f);
    (void)fx.Load("asset://anim/f.facts.sdata", kAnimFactSchemaType, R"({
        "slots": [ { "name": "Crouched", "kind": "bool" }, { "name": "Dead", "kind": "bool" } ] })");
    (void)fx.Load("asset://anim/f.requests.sdata", kAnimRequestSchemaType, R"({ "intents": [
        { "intent": "anim.intent.reload", "params": [ { "name": "shells", "kind": "int" } ] } ] })");
    (void)fx.Load("asset://anim/f.quick.flow.sdata", kAnimFlowType, R"({ "sections": [
        { "tag": "Anim.Reload.Close", "clip": "asset://anim/close.sanim" } ] })");
}

// The reload rig: a request-keyed layer where the reload request plays
// `flow` under the behavior's latch policy, and which idles otherwise.
// `reloadExtras` continues the reload behavior's object and `bindings` names a
// binding set for the rig, for content that announces what it plays.
inline DataAssetHandle LoadPumpReload(AnimRigFixture& fx, const std::string& flow,
                                      std::string_view onCancel = "cancel_section",
                                      std::string_view extraSlots = {}, std::string_view reloadExtras = {},
                                      std::string_view bindings = {})
{
    LoadFlowCommon(fx);
    (void)fx.Load("asset://anim/f.flow.sdata", kAnimFlowType, flow);
    (void)fx.Load("asset://anim/f.behaviors.sdata", kAnimBehaviorSetType,
                  std::format(R"({{ "behaviors": [
                      {{ "tag": "Anim.Idle", "kind": "cyclic" }},
                      {{ "tag": "Anim.Reload.Shell", "kind": "one_shot" }},
                      {{ "tag": "anim.intent.reload", "kind": "flow",
                         "latch": {{ "on_request_cancel": "{}" }} {} }} ] }})",
                              onCancel, reloadExtras));
    (void)fx.Load("asset://anim/f.slots.sdata", kAnimSlotMapType,
                  std::format(R"({{ "rows": [ {}
                      {{ "behavior": "Anim.Idle", "clip": "asset://anim/idle.sanim" }},
                      {{ "behavior": "anim.intent.reload", "flow": "asset://anim/f.flow.sdata" }} ] }})",
                              extraSlots));
    const std::string bindingList = bindings.empty() ? std::string() : std::format(R"("bindings": [ "{}" ],)", bindings);
    const DataAssetHandle rig = fx.Load("asset://anim/f.rig.sdata", kAnimRigType, std::format(R"({{
        "facts": "asset://anim/f.facts.sdata", "requests": "asset://anim/f.requests.sdata", {}
        "behaviors": [ "asset://anim/f.behaviors.sdata" ], "slot_maps": [ "asset://anim/f.slots.sdata" ],
        "layers": [ {{ "name": "anim.layer.base", "idle": "Anim.Idle" }} ] }})", bindingList));
    EXPECT_TRUE(fx.Bound(rig).Valid) << AnimRigFixture::Describe(fx.Bound(rig));
    return rig;
}

// The reload rig on one prop.
struct FlowProp : AnimRigFixture
{
    DataAssetHandle Rig;
    EntityId Entity;

    explicit FlowProp(const std::string& flow, std::string_view onCancel = "cancel_section",
                      std::string_view extraSlots = {})
    {
        Rig = LoadPumpReload(*this, flow, onCancel, extraSlots);
        Entity = Character(Rig);
    }

    AnimRequestResult Reload(int shells)
    {
        AnimRequestDesc desc;
        desc.Source = Entity;
        desc.Intent = Tag("anim.intent.reload");
        desc.Params[0] = AnimFactFromInt(shells);
        return IssueAnimRequest(Entities, Entity, desc, Now);
    }

    // Idle on tick 0, the reload issued for tick 1.
    AnimRequestId Start(int shells)
    {
        Tick();
        const AnimRequestResult reload = Reload(shells);
        EXPECT_TRUE(reload.Accepted());
        Tick();
        return reload.Id;
    }

    // Runs ticks up to and including `tick`.
    void TickTo(AnimTick tick)
    {
        while (Now <= tick)
            Tick();
    }

    const AnimLayerFlow& Flow(EntityId entity) const
    {
        const World& reader = Entities;
        return reader.TryGet<AnimFlowState>(entity)->Layers[0];
    }
    const AnimLayerFlow& Flow() const { return Flow(Entity); }

    std::string SectionName(EntityId entity)
    {
        const AnimBoundRig& rig = Bound(Rig);
        const AnimLayerContent& layer = Playing(entity);
        if (layer.Content >= rig.Contents.size() || rig.Contents[layer.Content].Flow < 0)
            return "(no flow)";
        const AnimBoundFlow& flow = rig.Flows[static_cast<std::size_t>(rig.Contents[layer.Content].Flow)];
        const std::uint8_t section = Flow(entity).Section;
        return section < flow.Sections.size() ? flow.Sections[section].TagName : std::string("(none)");
    }
    std::string SectionName() { return SectionName(Entity); }

    std::string PlayingClip(EntityId entity)
    {
        const AnimBoundRig& rig = Bound(Rig);
        const std::uint16_t clip = Playing(entity).Clip;
        return clip < rig.Contents.size() ? rig.Contents[clip].Path : std::string("(none)");
    }

    const AnimRequest* Request(EntityId entity, AnimRequestId id) const
    {
        const World& reader = Entities;
        for (const AnimRequest& request : reader.TryGet<AnimRequestSet>(entity)->Records)
            if (request.Occupied && request.Id == id)
                return &request;
        return nullptr;
    }

    std::vector<AnimDecisionRecord> SectionRecords(EntityId entity)
    {
        std::vector<AnimDecisionRecord> records;
        const AnimDecisionLog& log = Log(entity);
        for (std::size_t i = 0; i < log.Size(); ++i)
            if (log.At(i).Cause == AnimDecisionCause::SectionChanged)
                records.push_back(log.At(i));
        return records;
    }
};
