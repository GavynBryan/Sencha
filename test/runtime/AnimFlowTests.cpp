// Flows: forward-only sections that loop, branch and cancel on the tick clock,
// anchored on the request that drives them so a late joiner finds the same
// section, and announced as each section is entered and left.

#include "AnimRigFixture.h"

#include <anim/AnimEventSystem.h>
#include <anim/AnimFacts.h>
#include <authored/VerbDispatcher.h>
#include <authored/VerbRegistry.h>
#include <world/SimulationAuthority.h>

#include <gtest/gtest.h>

#include <format>
#include <string>
#include <utility>
#include <vector>

namespace
{
    constexpr const char* kFlowTags[] = {
        "Anim.Idle", "anim.intent.reload", "Anim.Reload", "Anim.Hit", "Anim.Reload.Shell",
        "Anim.Reload.Open", "Anim.Reload.Insert", "Anim.Reload.Close",
    };

    // Open for 15 ticks, insert for 30 a request's `shells` times, close for
    // 15; a cancel goes to the close.
    constexpr std::string_view kPumpFlow = R"({ "sections": [
        { "tag": "Anim.Reload.Open", "clip": "asset://anim/open.sanim" },
        { "tag": "Anim.Reload.Insert", "clip": "asset://anim/insert.sanim", "loop": "count",
          "count_intent": "anim.intent.reload", "count_param": "shells" CANCEL_TIMING },
        { "tag": "Anim.Reload.Close", "clip": "asset://anim/close.sanim" } ],
        "cancel": "Anim.Reload.Close" })";

    std::string PumpFlow(std::string_view insertCancelTiming = {})
    {
        std::string flow(kPumpFlow);
        const std::string timing =
            insertCancelTiming.empty() ? std::string() : std::format(R"(, "cancel_timing": "{}")", insertCancelTiming);
        flow.replace(flow.find("CANCEL_TIMING"), std::string_view("CANCEL_TIMING").size(), timing);
        return flow;
    }

    void LoadCommon(AnimRigFixture& fx)
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

    // A request-keyed layer: the reload request plays `flow` under the
    // behavior's latch policy, and the layer idles otherwise.
    struct FlowProp : AnimRigFixture
    {
        DataAssetHandle Rig;
        EntityId Entity;

        explicit FlowProp(const std::string& flow, std::string_view onCancel = "cancel_section",
                          std::string_view extraSlots = {})
        {
            LoadCommon(*this);
            (void)Load("asset://anim/f.flow.sdata", kAnimFlowType, flow);
            (void)Load("asset://anim/f.behaviors.sdata", kAnimBehaviorSetType,
                       std::format(R"({{ "behaviors": [
                           {{ "tag": "Anim.Idle", "kind": "cyclic" }},
                           {{ "tag": "Anim.Reload.Shell", "kind": "one_shot" }},
                           {{ "tag": "anim.intent.reload", "kind": "flow",
                              "latch": {{ "on_request_cancel": "{}" }} }} ] }})",
                                   onCancel));
            (void)Load("asset://anim/f.slots.sdata", kAnimSlotMapType,
                       std::format(R"({{ "rows": [ {}
                           {{ "behavior": "Anim.Idle", "clip": "asset://anim/idle.sanim" }},
                           {{ "behavior": "anim.intent.reload", "flow": "asset://anim/f.flow.sdata" }} ] }})",
                                   extraSlots));
            Rig = Load("asset://anim/f.rig.sdata", kAnimRigType, R"({
                "facts": "asset://anim/f.facts.sdata", "requests": "asset://anim/f.requests.sdata",
                "behaviors": [ "asset://anim/f.behaviors.sdata" ], "slot_maps": [ "asset://anim/f.slots.sdata" ],
                "layers": [ { "name": "anim.layer.base", "idle": "Anim.Idle" } ] })");
            EXPECT_TRUE(Bound(Rig).Valid) << Describe(Bound(Rig));
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
}

TEST(AnimFlowData, AFlowOnlyGoesForward)
{
    AnimRigFixture fx;
    const std::string error = fx.CompileError(kAnimFlowType, R"({ "sections": [
        { "tag": "Anim.A", "clip": "asset://anim/a.sanim" },
        { "tag": "Anim.B", "clip": "asset://anim/b.sanim", "branches": [ { "to": "Anim.A", "when": [] } ] } ] })");
    EXPECT_NE(error.find("$.data.sections[1].branches[0].to"), std::string::npos) << error;
    EXPECT_NE(error.find("only goes forward"), std::string::npos) << error;

    EXPECT_NE(fx.CompileError(kAnimFlowType, R"({ "sections": [
        { "tag": "Anim.A", "clip": "asset://anim/a.sanim", "slot": "Anim.B" } ] })")
                  .find("exactly one of a clip or a slot"),
              std::string::npos);
    EXPECT_NE(fx.CompileError(kAnimFlowType, R"({ "sections": [
        { "tag": "Anim.A", "clip": "asset://anim/a.sanim" } ], "cancel": "Anim.Z" })")
                  .find("$.data.cancel"),
              std::string::npos);
}

// Each section ends on the first tick at or past its clip's length and the
// next begins on that tick; a count loop plays its section as many times as
// the request says.
TEST(AnimFlow, SectionsFollowOnTheTickClockAndACountLoopPlaysTheRequestsCount)
{
    FlowProp fx(PumpFlow());
    (void)fx.Start(3);
    EXPECT_EQ(fx.SectionName(), "Anim.Reload.Open");
    EXPECT_EQ(fx.Flow().SectionStartTick, 1u);
    EXPECT_EQ(fx.PlayingClip(fx.Entity), "asset://anim/open.sanim");

    fx.TickTo(15);
    EXPECT_EQ(fx.SectionName(), "Anim.Reload.Open");
    fx.TickTo(16);
    EXPECT_EQ(fx.SectionName(), "Anim.Reload.Insert");
    EXPECT_EQ(fx.Flow().SectionStartTick, 16u);
    EXPECT_EQ(fx.Flow().LoopCount, 0u);
    EXPECT_EQ(fx.PlayingClip(fx.Entity), "asset://anim/insert.sanim");

    fx.TickTo(46);
    EXPECT_EQ(fx.Flow().LoopCount, 1u);
    EXPECT_EQ(fx.Flow().SectionStartTick, 46u);
    fx.TickTo(76);
    EXPECT_EQ(fx.Flow().LoopCount, 2u);
    fx.TickTo(105);
    EXPECT_EQ(fx.SectionName(), "Anim.Reload.Insert");
    fx.TickTo(106);
    EXPECT_EQ(fx.SectionName(), "Anim.Reload.Close");

    fx.TickTo(120);
    EXPECT_EQ(fx.Flow().Phase, AnimFlowPhase::Playing);
    EXPECT_FALSE(fx.Playing(fx.Entity).ContentComplete);
    fx.TickTo(121);
    EXPECT_EQ(fx.Flow().Phase, AnimFlowPhase::Complete);
    EXPECT_TRUE(fx.Playing(fx.Entity).ContentComplete);
    EXPECT_FLOAT_EQ(fx.Playing(fx.Entity).TimeSeconds, 0.25f);

    // A completed flow holds its last pose while its request lives; it does
    // not start over.
    fx.TickTo(200);
    EXPECT_EQ(fx.Flow().Phase, AnimFlowPhase::Complete);
    EXPECT_EQ(fx.BehaviorName(fx.Entity), "anim.intent.reload");

    std::vector<AnimChangeReason> reasons;
    for (const AnimDecisionRecord& record : fx.SectionRecords(fx.Entity))
        reasons.push_back(record.Reason);
    EXPECT_EQ(reasons, (std::vector<AnimChangeReason>{ AnimChangeReason::FlowStarted,
                                                       AnimChangeReason::SectionFollowed,
                                                       AnimChangeReason::SectionLooped,
                                                       AnimChangeReason::SectionLooped,
                                                       AnimChangeReason::SectionFollowed }));
}

namespace
{
    // Open branches to Close while crouched; Insert repeats while dead and
    // ends the flow when it stops, so Close is reached only by the branch.
    constexpr std::string_view kBranchFlow = R"({ "sections": [
        { "tag": "Anim.Reload.Open", "clip": "asset://anim/open.sanim",
          "branches": [ { "to": "Anim.Reload.Close", "when": [ { "fact": "Crouched" } ] } ] },
        { "tag": "Anim.Reload.Insert", "clip": "asset://anim/insert.sanim", "loop": "while",
          "while": [ { "fact": "Dead" } ], "ends": true },
        { "tag": "Anim.Reload.Close", "clip": "asset://anim/close.sanim" } ] })";
}

TEST(AnimFlow, AWhileLoopRepeatsWhileItHoldsAndAnEndingSectionEndsTheFlow)
{
    FlowProp fx{ std::string(kBranchFlow) };
    fx.Motion(fx.Entity).Dead = true;
    (void)fx.Start(1);
    fx.TickTo(46);
    EXPECT_EQ(fx.SectionName(), "Anim.Reload.Insert");
    EXPECT_EQ(fx.Flow().LoopCount, 1u);

    fx.Motion(fx.Entity).Dead = false;
    fx.TickTo(75);
    EXPECT_EQ(fx.Flow().Phase, AnimFlowPhase::Playing);
    fx.TickTo(76);
    EXPECT_EQ(fx.Flow().Phase, AnimFlowPhase::Complete);
    EXPECT_EQ(fx.SectionName(), "Anim.Reload.Insert");
    for (const AnimDecisionRecord& record : fx.SectionRecords(fx.Entity))
        EXPECT_NE(record.Section, 2u) << "the close is reached only by the branch";
}

TEST(AnimFlow, ABranchTakenAtTheSectionsEndSkipsForward)
{
    FlowProp fx{ std::string(kBranchFlow) };
    fx.Motion(fx.Entity).Crouched = true;
    (void)fx.Start(1);
    fx.TickTo(16);
    EXPECT_EQ(fx.SectionName(), "Anim.Reload.Close");
    const std::vector<AnimDecisionRecord> records = fx.SectionRecords(fx.Entity);
    ASSERT_FALSE(records.empty());
    EXPECT_EQ(records.back().Reason, AnimChangeReason::SectionBranched);
    EXPECT_EQ(records.back().PreviousSection, 0u);
    fx.TickTo(31);
    EXPECT_EQ(fx.Flow().Phase, AnimFlowPhase::Complete);
}

// A cancelled request's flow finishes the section it is in, plays the cancel
// section, and only then lets the layer go; the request stays meanwhile so a
// late joiner still sees what is playing.
TEST(AnimFlow, ACancelAtSectionEndPlaysTheCancelSectionOnTheRequestsTail)
{
    FlowProp fx(PumpFlow());
    const AnimRequestId reload = fx.Start(3);
    fx.TickTo(50);
    ASSERT_TRUE(CancelAnimRequest(fx.Entities, fx.Entity, reload, AnimCancelReason::Released, fx.Now));

    fx.TickTo(75);
    EXPECT_EQ(fx.SectionName(), "Anim.Reload.Insert");
    const AnimRequest* request = fx.Request(fx.Entity, reload);
    ASSERT_NE(request, nullptr);
    EXPECT_TRUE(request->IsCancelled());
    EXPECT_TRUE(IsAnimRequestRetained(*request, fx.Now));

    fx.TickTo(76);
    EXPECT_EQ(fx.SectionName(), "Anim.Reload.Close");
    EXPECT_EQ(fx.Flow().SectionStartTick, 76u);
    EXPECT_EQ(fx.SectionRecords(fx.Entity).back().Reason, AnimChangeReason::SectionCancelled);

    fx.TickTo(90);
    EXPECT_EQ(fx.BehaviorName(fx.Entity), "anim.intent.reload");
    fx.TickTo(91);
    EXPECT_EQ(fx.Flow().Phase, AnimFlowPhase::Complete);
    fx.TickTo(92);
    EXPECT_EQ(fx.BehaviorName(fx.Entity), "Anim.Idle");
    EXPECT_EQ(fx.Flow().Phase, AnimFlowPhase::None);
}

TEST(AnimFlow, AnImmediateCancelGoesToTheCancelSectionAtOnce)
{
    FlowProp fx(PumpFlow("immediate"));
    const AnimRequestId reload = fx.Start(3);
    fx.TickTo(50);
    ASSERT_TRUE(CancelAnimRequest(fx.Entities, fx.Entity, reload, AnimCancelReason::Released, fx.Now));
    fx.TickTo(51);
    EXPECT_EQ(fx.SectionName(), "Anim.Reload.Close");
    EXPECT_EQ(fx.Flow().SectionStartTick, 51u);
    fx.TickTo(66);
    EXPECT_EQ(fx.Flow().Phase, AnimFlowPhase::Complete);
    fx.TickTo(67);
    EXPECT_EQ(fx.BehaviorName(fx.Entity), "Anim.Idle");
}

TEST(AnimFlow, AFinishPolicyPlaysTheFlowOutAndAnAbortDropsIt)
{
    FlowProp finish(PumpFlow(), "finish");
    const AnimRequestId finishing = finish.Start(2);
    finish.TickTo(20);
    ASSERT_TRUE(CancelAnimRequest(finish.Entities, finish.Entity, finishing, AnimCancelReason::Released, finish.Now));
    // Both inserts and the close still play: the cancelled request keeps its
    // count.
    finish.TickTo(75);
    EXPECT_EQ(finish.SectionName(), "Anim.Reload.Insert");
    EXPECT_EQ(finish.Flow().LoopCount, 1u);
    finish.TickTo(76);
    EXPECT_EQ(finish.SectionName(), "Anim.Reload.Close");
    finish.TickTo(91);
    EXPECT_EQ(finish.Flow().Phase, AnimFlowPhase::Complete);
    finish.TickTo(92);
    EXPECT_EQ(finish.BehaviorName(finish.Entity), "Anim.Idle");

    FlowProp abort(PumpFlow(), "abort");
    const AnimRequestId aborted = abort.Start(2);
    abort.TickTo(20);
    ASSERT_TRUE(CancelAnimRequest(abort.Entities, abort.Entity, aborted, AnimCancelReason::Released, abort.Now));
    abort.TickTo(21);
    EXPECT_EQ(abort.BehaviorName(abort.Entity), "Anim.Idle");
}

// The authority writes the section the flow is in on its request; a machine
// that joins later starts there, not at the top, and arrives where the
// authority is.
TEST(AnimFlow, ALateJoinerStartsAtTheRequestsAnchor)
{
    FlowProp fx(PumpFlow());
    const AnimRequestId reload = fx.Start(3);
    fx.TickTo(50);
    const AnimRequest* request = fx.Request(fx.Entity, reload);
    ASSERT_NE(request, nullptr);
    // Loops do not move the anchor: the joiner replays them from it.
    EXPECT_EQ(request->AnchorSection, 1u);
    EXPECT_EQ(request->AnchorSectionStartTick, 16u);

    const EntityId joiner = fx.Character(fx.Rig);
    const AnimRequestSet requests = *static_cast<const World&>(fx.Entities).TryGet<AnimRequestSet>(fx.Entity);
    *fx.Entities.TryGet<AnimRequestSet>(joiner) = requests;
    fx.TickTo(51);

    EXPECT_EQ(fx.Flow(joiner).Section, fx.Flow().Section);
    EXPECT_EQ(fx.Flow(joiner).SectionStartTick, fx.Flow().SectionStartTick);
    EXPECT_EQ(fx.Flow(joiner).LoopCount, fx.Flow().LoopCount);
    EXPECT_FLOAT_EQ(fx.Playing(joiner).TimeSeconds, fx.Playing(fx.Entity).TimeSeconds);
    EXPECT_EQ(fx.SectionRecords(joiner).front().Reason, AnimChangeReason::FlowAnchored);
}

TEST(AnimFlow, OnlyTheAuthorityWritesTheAnchor)
{
    FlowProp fx(PumpFlow());
    fx.Entities.SetResource(SimulationAuthority{ false });
    const AnimRequestId reload = fx.Start(3);
    fx.TickTo(20);
    EXPECT_EQ(fx.SectionName(), "Anim.Reload.Insert");
    EXPECT_EQ(fx.Request(fx.Entity, reload)->AnchorSection, kAnimNoAnchorSection);
}

// A request superseding the one a flow plays keeps the flow when it resolves
// the same row, and takes its anchor with it; resolving another row starts
// that row's flow over from the new request.
TEST(AnimFlow, ASupersedingRequestContinuesTheSameRowAndRestartsAnother)
{
    FlowProp fx(PumpFlow(), "cancel_section", R"(
        { "behavior": "anim.intent.reload", "when": [ { "fact": "Crouched" } ],
          "flow": "asset://anim/f.quick.flow.sdata" },)");
    (void)fx.Start(3);
    fx.TickTo(20);

    const AnimRequestResult again = fx.Reload(3);
    ASSERT_EQ(again.Status, AnimRequestStatus::Superseded);
    fx.TickTo(21);
    EXPECT_EQ(fx.SectionName(), "Anim.Reload.Insert");
    EXPECT_EQ(fx.Flow().SectionStartTick, 16u);
    EXPECT_EQ(fx.Playing(fx.Entity).Request, again.Id);
    const AnimRequest* adopted = fx.Request(fx.Entity, again.Id);
    ASSERT_NE(adopted, nullptr);
    EXPECT_EQ(adopted->AnchorSection, 1u);
    EXPECT_EQ(adopted->AnchorSectionStartTick, 16u);

    // Pinned: crouching does not change the flow already playing.
    fx.Motion(fx.Entity).Crouched = true;
    fx.TickTo(30);
    EXPECT_EQ(fx.ClipName(fx.Entity, fx.Bound(fx.Rig)), "asset://anim/f.flow.sdata");

    const AnimRequestResult crouched = fx.Reload(3);
    fx.TickTo(31);
    EXPECT_EQ(fx.ClipName(fx.Entity, fx.Bound(fx.Rig)), "asset://anim/f.quick.flow.sdata");
    EXPECT_EQ(fx.Flow().Section, 0u);
    EXPECT_EQ(fx.Flow().SectionStartTick, 31u);
    EXPECT_EQ(fx.Playing(fx.Entity).Request, crouched.Id);
    const AnimDecisionRecord* superseded = fx.LastRecord(fx.Entity, AnimDecisionCause::ContentChanged);
    ASSERT_NE(superseded, nullptr);
    EXPECT_EQ(superseded->Reason, AnimChangeReason::RequestSuperseded);
}

// A slot section resolves its slot when it is entered and keeps that clip for
// the section; the next pass through it resolves again.
TEST(AnimFlow, ASlotSectionResolvesOnEntry)
{
    FlowProp fx(R"({ "sections": [
        { "tag": "Anim.Reload.Open", "clip": "asset://anim/open.sanim" },
        { "tag": "Anim.Reload.Insert", "slot": "Anim.Reload.Shell", "loop": "count",
          "count_intent": "anim.intent.reload", "count_param": "shells" } ] })",
                "cancel_section", R"(
        { "behavior": "Anim.Reload.Shell", "when": [ { "fact": "Crouched" } ],
          "clip": "asset://anim/insert_crouch.sanim" },
        { "behavior": "Anim.Reload.Shell", "clip": "asset://anim/insert.sanim" },)");
    (void)fx.Start(2);
    fx.TickTo(16);
    EXPECT_EQ(fx.PlayingClip(fx.Entity), "asset://anim/insert.sanim");
    fx.Motion(fx.Entity).Crouched = true;
    fx.TickTo(45);
    EXPECT_EQ(fx.PlayingClip(fx.Entity), "asset://anim/insert.sanim");
    fx.TickTo(46);
    EXPECT_EQ(fx.PlayingClip(fx.Entity), "asset://anim/insert_crouch.sanim");
}

namespace
{
    // The same reload on a selector layer: latched until its request ends,
    // interruptible by a hit, and playing the close for either.
    struct FlowCharacter : FlowProp
    {
        FlowCharacter()
            : FlowProp(PumpFlow())
        {
            (void)Load("asset://anim/c.behaviors.sdata", kAnimBehaviorSetType, R"({ "behaviors": [
                { "tag": "Anim.Idle", "kind": "cyclic" },
                { "tag": "Anim.Reload", "kind": "flow",
                  "latch": { "mode": "until_request_ends", "interruptible_by": "tags", "tags": [ "Anim.Hit" ],
                             "on_interrupt": "cancel_section", "on_request_cancel": "cancel_section" } },
                { "tag": "Anim.Hit", "kind": "hold" } ] })");
            (void)Load("asset://anim/c.selector.sdata", kAnimSelectorType, R"({ "rules": [
                { "name": "idle", "priority": 0, "enter": [], "behavior": "Anim.Idle" },
                { "name": "reload", "priority": 50, "enter": [ { "request": "anim.intent.reload" } ],
                  "behavior": "Anim.Reload" },
                { "name": "hit", "priority": 100, "enter": [ { "fact": "Dead" } ], "behavior": "Anim.Hit" } ] })");
            (void)Load("asset://anim/c.slots.sdata", kAnimSlotMapType, R"({ "rows": [
                { "behavior": "Anim.Idle", "clip": "asset://anim/idle.sanim" },
                { "behavior": "Anim.Reload", "flow": "asset://anim/f.flow.sdata" },
                { "behavior": "Anim.Hit", "clip": "asset://anim/hit.sanim" } ] })");
            Rig = Load("asset://anim/c.rig.sdata", kAnimRigType, R"({
                "facts": "asset://anim/f.facts.sdata", "requests": "asset://anim/f.requests.sdata",
                "behaviors": [ "asset://anim/c.behaviors.sdata" ], "slot_maps": [ "asset://anim/c.slots.sdata" ],
                "layers": [ { "name": "anim.layer.base", "selector": "asset://anim/c.selector.sdata",
                              "idle": "Anim.Idle" } ] })");
            EXPECT_TRUE(Bound(Rig).Valid) << Describe(Bound(Rig));
            Entity = Character(Rig);
        }
    };
}

TEST(AnimFlow, AnInterruptThatCancelsTheSectionWaitsForTheCancelSection)
{
    FlowCharacter fx;
    (void)fx.Start(3);
    fx.TickTo(20);
    EXPECT_EQ(fx.BehaviorName(fx.Entity), "Anim.Reload");

    fx.Motion(fx.Entity).Dead = true;
    fx.TickTo(21);
    EXPECT_EQ(fx.Selection(fx.Entity).Latch, AnimLatchState::Cancelling);
    EXPECT_EQ(fx.BehaviorName(fx.Entity), "Anim.Reload");
    EXPECT_NE(fx.LastRecord(fx.Entity, AnimDecisionCause::LatchInterrupted), nullptr);

    fx.TickTo(46);
    EXPECT_EQ(fx.SectionName(), "Anim.Reload.Close");
    fx.TickTo(61);
    EXPECT_EQ(fx.Flow().Phase, AnimFlowPhase::Complete);
    EXPECT_EQ(fx.BehaviorName(fx.Entity), "Anim.Reload");
    fx.TickTo(62);
    EXPECT_EQ(fx.BehaviorName(fx.Entity), "Anim.Hit");
}

TEST(AnimFlow, ACancelledLatchRequestPlaysTheCancelSectionOnASelectorLayer)
{
    FlowCharacter fx;
    const AnimRequestId reload = fx.Start(3);
    fx.TickTo(20);
    ASSERT_TRUE(CancelAnimRequest(fx.Entities, fx.Entity, reload, AnimCancelReason::Released, fx.Now));
    fx.TickTo(21);
    EXPECT_EQ(fx.Selection(fx.Entity).Latch, AnimLatchState::Cancelling);
    fx.TickTo(46);
    EXPECT_EQ(fx.SectionName(), "Anim.Reload.Close");
    EXPECT_TRUE(IsAnimRequestRetained(*fx.Request(fx.Entity, reload), fx.Last()));
    fx.TickTo(62);
    EXPECT_EQ(fx.BehaviorName(fx.Entity), "Anim.Idle");
}

namespace
{
    DataAssetHandle BindingRig(AnimRigFixture& fx, std::string_view layerIdle, std::string_view kind,
                           std::string_view selector)
    {
        LoadCommon(fx);
        (void)fx.Load("asset://anim/v.flow.sdata", kAnimFlowType, R"({ "sections": [
            { "tag": "Anim.Reload.Insert", "clip": "asset://anim/insert.sanim", "loop": "while",
              "while": [ { "fact": "Dead" } ] } ] })");
        (void)fx.Load("asset://anim/v.behaviors.sdata", kAnimBehaviorSetType,
                      std::format(R"({{ "behaviors": [ {{ "tag": "Anim.Idle", "kind": "cyclic" }},
                          {{ "tag": "Anim.Reload", "kind": "{}" }} ] }})",
                                  kind));
        (void)fx.Load("asset://anim/v.slots.sdata", kAnimSlotMapType, R"({ "rows": [
            { "behavior": "Anim.Idle", "clip": "asset://anim/idle.sanim" },
            { "behavior": "Anim.Reload", "flow": "asset://anim/v.flow.sdata" } ] })");
        std::string layer = std::format(R"({{ "name": "anim.layer.base", "idle": "{}" }})", layerIdle);
        if (!selector.empty())
        {
            (void)fx.Load("asset://anim/v.selector.sdata", kAnimSelectorType, selector);
            layer = std::format(R"({{ "name": "anim.layer.base", "idle": "{}",
                                     "selector": "asset://anim/v.selector.sdata" }})",
                                layerIdle);
        }
        return fx.Load("asset://anim/v.rig.sdata", kAnimRigType,
                      std::format(R"({{ "facts": "asset://anim/f.facts.sdata",
                          "requests": "asset://anim/f.requests.sdata",
                          "behaviors": [ "asset://anim/v.behaviors.sdata" ],
                          "slot_maps": [ "asset://anim/v.slots.sdata" ], "layers": [ {} ] }})",
                                  layer));
    }
}

// A loop or an immediate cancel is only reconstructible from a request's
// anchor, so a flow that has either must be reached through a request.
TEST(AnimFlowBinding, ALoopingFlowMustBeReachedThroughARequest)
{
    {
        AnimRigFixture fx;
        const AnimBoundRig& rig = fx.Bound(BindingRig(fx, "Anim.Reload", "flow", {}));
        const AnimDiagnostic* idle = AnimRigFixture::FindCode(rig, "anim.flow.cosmetic_loop");
        ASSERT_NE(idle, nullptr) << AnimRigFixture::Describe(rig);
        EXPECT_EQ(idle->AssetPath, "asset://anim/v.slots.sdata");
        EXPECT_EQ(idle->FieldPath, "$.data.rows[1].flow");
    }
    {
        AnimRigFixture fx;
        const AnimBoundRig& rig = fx.Bound(BindingRig(fx, "Anim.Idle", "flow", R"({ "rules": [
            { "name": "reload", "priority": 50, "enter": [ { "fact": "Crouched" } ], "behavior": "Anim.Reload" } ] })"));
        EXPECT_NE(AnimRigFixture::FindCode(rig, "anim.flow.cosmetic_loop"), nullptr) << AnimRigFixture::Describe(rig);
    }
    {
        AnimRigFixture fx;
        const AnimBoundRig& rig = fx.Bound(BindingRig(fx, "Anim.Idle", "flow", R"({ "rules": [
            { "name": "reload", "priority": 50, "enter": [ { "request": "anim.intent.reload" } ],
              "behavior": "Anim.Reload" } ] })"));
        EXPECT_EQ(AnimRigFixture::FindCode(rig, "anim.flow.cosmetic_loop"), nullptr) << AnimRigFixture::Describe(rig);
        EXPECT_TRUE(rig.Valid) << AnimRigFixture::Describe(rig);
    }
}

TEST(AnimFlowBinding, AFlowRowsBehaviorIsAFlow)
{
    AnimRigFixture fx;
    const AnimBoundRig& rig = fx.Bound(BindingRig(fx, "Anim.Idle", "one_shot", R"({ "rules": [
        { "name": "reload", "priority": 50, "enter": [ { "request": "anim.intent.reload" } ],
          "behavior": "Anim.Reload" } ] })"));
    const AnimDiagnostic* kind = AnimRigFixture::FindCode(rig, "anim.flow.behavior_kind");
    ASSERT_NE(kind, nullptr) << AnimRigFixture::Describe(rig);
    EXPECT_FALSE(rig.Valid);
}

TEST(AnimFlowBinding, ACountLoopReadsAnIntParameterOfItsIntent)
{
    AnimRigFixture fx;
    LoadCommon(fx);
    (void)fx.Load("asset://anim/n.requests.sdata", kAnimRequestSchemaType, R"({ "intents": [
        { "intent": "anim.intent.reload", "params": [ { "name": "shells", "kind": "float" } ] } ] })");
    (void)fx.Load("asset://anim/n.flow.sdata", kAnimFlowType, R"({ "sections": [
        { "tag": "Anim.Reload.Insert", "clip": "asset://anim/insert.sanim", "loop": "count",
          "count_intent": "anim.intent.reload", "count_param": "shells" } ] })");
    (void)fx.Load("asset://anim/n.behaviors.sdata", kAnimBehaviorSetType, R"({ "behaviors": [
        { "tag": "Anim.Idle", "kind": "cyclic" }, { "tag": "anim.intent.reload", "kind": "flow" } ] })");
    (void)fx.Load("asset://anim/n.slots.sdata", kAnimSlotMapType, R"({ "rows": [
        { "behavior": "Anim.Idle", "clip": "asset://anim/idle.sanim" },
        { "behavior": "anim.intent.reload", "flow": "asset://anim/n.flow.sdata" } ] })");
    const AnimBoundRig& rig = fx.Bound(fx.Load("asset://anim/n.rig.sdata", kAnimRigType, R"({
        "requests": "asset://anim/n.requests.sdata", "behaviors": [ "asset://anim/n.behaviors.sdata" ],
        "slot_maps": [ "asset://anim/n.slots.sdata" ],
        "layers": [ { "name": "anim.layer.base", "idle": "Anim.Idle" } ] })"));
    const AnimDiagnostic* param = AnimRigFixture::FindCode(rig, "anim.flow.count_param");
    ASSERT_NE(param, nullptr) << AnimRigFixture::Describe(rig);
    EXPECT_EQ(param->AssetPath, "asset://anim/n.flow.sdata");
    EXPECT_EQ(param->FieldPath, "$.data.sections[0].count_param");
}

namespace
{
    DataFieldSchema TagField(std::string key)
    {
        DataFieldSchema field;
        field.Key = std::move(key);
        field.Kind = DataFieldKind::GameplayTag;
        return field;
    }

    // Records each lifecycle invocation's binding and the tag it was handed.
    struct TagRecorder
    {
        std::vector<std::pair<VerbBindingKey, GameplayTagId>> Calls;

        VerbAdmission Invoke(const VerbInvocation& invocation)
        {
            GameplayTagId tag;
            (void)invocation.Arguments->TryGetTag(0, tag);
            Calls.emplace_back(invocation.Binding, tag);
            return VerbAdmission::Accepted;
        }
    };
}

// Sections announce themselves inside their behavior: entering the behavior
// comes before its first section, leaving the last section before leaving the
// behavior. A loop replays its section without leaving it.
TEST(AnimFlowEvents, SectionsAreAnnouncedInsideTheirBehavior)
{
    AnimRigFixture fx;
    LoadCommon(fx);
    VerbRegistry& verbs = fx.Verbs();
    {
        VerbDefinition definition;
        definition.Name = "test.mark";
        definition.Arguments.Children = { TagField("Tag") };
        VerbRegistrationScope scope(verbs, "test");
        (void)scope.Declare(std::move(definition));
        ASSERT_TRUE(scope.Commit());
    }
    VerbDispatcher dispatcher(verbs);
    TagRecorder recorder;
    const VerbBindingToken token = dispatcher.Bind(verbs.Find("test.mark"), recorder);
    AnimEventSystem events(&dispatcher, true);

    (void)fx.Load("asset://anim/e.bindings.sdata", kVerbBindingsTypeName, R"({ "bindings": [
        { "key": "anim.entered", "verb": "test.mark", "inputs": [ "behavior" ],
          "arguments": { "Tag": { "input": "behavior" } } },
        { "key": "anim.exited", "verb": "test.mark", "inputs": [ "behavior" ],
          "arguments": { "Tag": { "input": "behavior" } } },
        { "key": "anim.section.entered", "verb": "test.mark", "inputs": [ "section" ],
          "arguments": { "Tag": { "input": "section" } } },
        { "key": "anim.section.exited", "verb": "test.mark", "inputs": [ "section" ],
          "arguments": { "Tag": { "input": "section" } } } ] })");
    std::string flow = PumpFlow();
    flow.insert(flow.rfind('}'), R"(, "on_section_entered": { "binding": "anim.section.entered" },
        "on_section_exited": { "binding": "anim.section.exited" })");
    (void)fx.Load("asset://anim/e.flow.sdata", kAnimFlowType, flow);
    (void)fx.Load("asset://anim/e.behaviors.sdata", kAnimBehaviorSetType, R"({ "behaviors": [
        { "tag": "Anim.Idle", "kind": "cyclic" },
        { "tag": "anim.intent.reload", "kind": "flow", "on_entered": { "binding": "anim.entered" },
          "on_exited": { "binding": "anim.exited" } } ] })");
    (void)fx.Load("asset://anim/e.slots.sdata", kAnimSlotMapType, R"({ "rows": [
        { "behavior": "Anim.Idle", "clip": "asset://anim/idle.sanim" },
        { "behavior": "anim.intent.reload", "flow": "asset://anim/e.flow.sdata" } ] })");
    const DataAssetHandle rig = fx.Load("asset://anim/e.rig.sdata", kAnimRigType, R"({
        "requests": "asset://anim/f.requests.sdata", "behaviors": [ "asset://anim/e.behaviors.sdata" ],
        "slot_maps": [ "asset://anim/e.slots.sdata" ], "bindings": [ "asset://anim/e.bindings.sdata" ],
        "layers": [ { "name": "anim.layer.base", "idle": "Anim.Idle" } ] })");
    ASSERT_TRUE(fx.Bound(rig).Valid) << AnimRigFixture::Describe(fx.Bound(rig));
    ASSERT_TRUE(fx.Bound(rig).Diagnostics.empty()) << AnimRigFixture::Describe(fx.Bound(rig));

    const EntityId prop = fx.Entities.CreateEntity();
    fx.Entities.AddComponent(prop, AnimRig{ rig });
    fx.Entities.AddComponent(prop, AnimDecisionLog{});
    const auto step = [&](AnimTick through) {
        while (fx.Now <= through)
        {
            fx.Tick();
            events.Run(fx.Entities, fx.Last(), AnimRigFixture::kTick);
        }
    };
    using Call = std::pair<VerbBindingKey, GameplayTagId>;
    const auto call = [&](std::string_view binding, std::string_view tag) {
        return Call{ MakeVerbBindingKey(binding), fx.Tag(tag) };
    };

    step(0);
    AnimRequestDesc desc;
    desc.Source = prop;
    desc.Intent = fx.Tag("anim.intent.reload");
    desc.Params[0] = AnimFactFromInt(2);
    const AnimRequestResult reload = IssueAnimRequest(fx.Entities, prop, desc, fx.Now);
    ASSERT_TRUE(reload.Accepted());
    step(1);
    EXPECT_EQ(recorder.Calls, (std::vector<Call>{ call("anim.entered", "anim.intent.reload"),
                                                  call("anim.section.entered", "Anim.Reload.Open") }));

    recorder.Calls.clear();
    step(16);
    EXPECT_EQ(recorder.Calls, (std::vector<Call>{ call("anim.section.exited", "Anim.Reload.Open"),
                                                  call("anim.section.entered", "Anim.Reload.Insert") }));

    recorder.Calls.clear();
    step(75);
    EXPECT_TRUE(recorder.Calls.empty()) << "a loop replays its section without leaving it";
    step(76);
    EXPECT_EQ(recorder.Calls, (std::vector<Call>{ call("anim.section.exited", "Anim.Reload.Insert"),
                                                  call("anim.section.entered", "Anim.Reload.Close") }));

    recorder.Calls.clear();
    step(100);
    ASSERT_TRUE(CancelAnimRequest(fx.Entities, prop, reload.Id, AnimCancelReason::Released, fx.Now));
    step(101);
    EXPECT_EQ(recorder.Calls, (std::vector<Call>{ call("anim.section.exited", "Anim.Reload.Close"),
                                                  call("anim.exited", "anim.intent.reload") }));

    const AnimDecisionRecord* entered = fx.LastRecord(prop, AnimDecisionCause::SectionEntered);
    ASSERT_NE(entered, nullptr);
    EXPECT_EQ(entered->Section, 2u);
    (void)token;
}

namespace
{
    // A request-keyed base layer and a selector upper layer both driven by
    // the reload request, playing `baseFlow` and `upperFlow`.
    const AnimBoundRig& BindTwoLayerReload(AnimRigFixture& fx, std::string_view baseFlow, std::string_view upperFlow)
    {
        LoadCommon(fx);
        (void)fx.Load("asset://anim/f.flow.sdata", kAnimFlowType, PumpFlow());
        (void)fx.Tags().RegisterTag("anim.layer.upper");
        (void)fx.Load("asset://anim/t.behaviors.sdata", kAnimBehaviorSetType, R"({ "behaviors": [
            { "tag": "Anim.Idle", "kind": "cyclic" }, { "tag": "anim.intent.reload", "kind": "flow" },
            { "tag": "Anim.Reload", "kind": "flow", "latch": { "mode": "until_request_ends" } } ] })");
        (void)fx.Load("asset://anim/t.slots.sdata", kAnimSlotMapType,
                      std::format(R"({{ "rows": [
            {{ "behavior": "Anim.Idle", "clip": "asset://anim/idle.sanim" }},
            {{ "behavior": "anim.intent.reload", "flow": "{}" }},
            {{ "behavior": "Anim.Reload", "flow": "{}" }} ] }})",
                                  baseFlow, upperFlow));
        (void)fx.Load("asset://anim/t.upper.sdata", kAnimSelectorType, R"({ "rules": [
            { "name": "reload", "priority": 50, "enter": [ { "request": "anim.intent.reload" } ],
              "behavior": "Anim.Reload" },
            { "name": "idle", "priority": 0, "enter": [], "behavior": "Anim.Idle" } ] })");
        return fx.Bound(fx.Load("asset://anim/t.rig.sdata", kAnimRigType, R"({
            "facts": "asset://anim/f.facts.sdata", "requests": "asset://anim/f.requests.sdata",
            "behaviors": [ "asset://anim/t.behaviors.sdata" ], "slot_maps": [ "asset://anim/t.slots.sdata" ],
            "layers": [ { "name": "anim.layer.base", "idle": "Anim.Idle" },
                        { "name": "anim.layer.upper", "selector": "asset://anim/t.upper.sdata",
                          "idle": "Anim.Idle" } ] })"));
    }
}

// One request carries one anchor, so the flows it drives on several layers
// must agree on what a section index and its start tick mean.
TEST(AnimFlowBinding, FlowsSharingARequestShareTheirSectionTiming)
{
    {
        AnimRigFixture fx;
        const AnimBoundRig& rig =
            BindTwoLayerReload(fx, "asset://anim/f.quick.flow.sdata", "asset://anim/f.flow.sdata");
        const AnimDiagnostic* timing = AnimRigFixture::FindCode(rig, "anim.flow.anchor_timing");
        ASSERT_NE(timing, nullptr) << AnimRigFixture::Describe(rig);
        EXPECT_NE(timing->Message.find("one anchor"), std::string::npos) << timing->Message;
        EXPECT_NE(timing->Message.find("1 and 3 sections"), std::string::npos) << timing->Message;
        EXPECT_FALSE(rig.Valid);
    }
    {
        AnimRigFixture fx;
        const AnimBoundRig& rig = BindTwoLayerReload(fx, "asset://anim/f.flow.sdata", "asset://anim/f.flow.sdata");
        EXPECT_EQ(AnimRigFixture::FindCode(rig, "anim.flow.anchor_timing"), nullptr) << AnimRigFixture::Describe(rig);
        EXPECT_TRUE(rig.Valid) << AnimRigFixture::Describe(rig);
    }
}

// A row choosing its flow by a local fact picks per machine, so the flows it
// chooses between must keep the same time.
TEST(AnimFlowBinding, RowsReadingLocalFactsChooseBetweenFlowsThatKeepTheSameTime)
{
    AnimRigFixture fx;
    LoadCommon(fx);
    (void)fx.Load("asset://anim/f.flow.sdata", kAnimFlowType, PumpFlow());
    (void)fx.Load("asset://anim/lf.facts.sdata", kAnimFactSchemaType, R"({
        "slots": [ { "name": "Variant", "kind": "int", "local": true } ] })");
    (void)fx.Load("asset://anim/lf.behaviors.sdata", kAnimBehaviorSetType, R"({ "behaviors": [
        { "tag": "Anim.Idle", "kind": "cyclic" }, { "tag": "anim.intent.reload", "kind": "flow" } ] })");
    (void)fx.Load("asset://anim/lf.slots.sdata", kAnimSlotMapType, R"({ "rows": [
        { "behavior": "Anim.Idle", "clip": "asset://anim/idle.sanim" },
        { "behavior": "anim.intent.reload", "when": [ { "fact": "Variant", "compare": "eq", "value": 1 } ],
          "flow": "asset://anim/f.quick.flow.sdata" },
        { "behavior": "anim.intent.reload", "flow": "asset://anim/f.flow.sdata" } ] })");
    const AnimBoundRig& rig = fx.Bound(fx.Load("asset://anim/lf.rig.sdata", kAnimRigType, R"({
        "facts": "asset://anim/lf.facts.sdata", "requests": "asset://anim/f.requests.sdata",
        "behaviors": [ "asset://anim/lf.behaviors.sdata" ], "slot_maps": [ "asset://anim/lf.slots.sdata" ],
        "layers": [ { "name": "anim.layer.base", "idle": "Anim.Idle" } ] })"));
    const AnimDiagnostic* timing = AnimRigFixture::FindCode(rig, "anim.slot.local_timing");
    ASSERT_NE(timing, nullptr) << AnimRigFixture::Describe(rig);
    EXPECT_EQ(timing->FieldPath, "$.data.rows[1].when");
    EXPECT_NE(timing->Message.find("sections"), std::string::npos) << timing->Message;
}

// A combo advances by superseding its request. On a selector layer the latch
// follows the request that replaced its own in place, so the new request
// drives the flow and its row -- the next swing -- starts over.
TEST(AnimFlow, ALatchFollowsTheRequestThatSupersedesItsOwn)
{
    AnimRigFixture fx;
    LoadCommon(fx);
    (void)fx.Load("asset://anim/f.flow.sdata", kAnimFlowType, PumpFlow());
    (void)fx.Load("asset://anim/s.behaviors.sdata", kAnimBehaviorSetType, R"({ "behaviors": [
        { "tag": "Anim.Idle", "kind": "cyclic" },
        { "tag": "Anim.Reload", "kind": "flow",
          "latch": { "mode": "until_request_ends", "on_request_cancel": "finish" } } ] })");
    (void)fx.Load("asset://anim/s.slots.sdata", kAnimSlotMapType, R"({ "rows": [
        { "behavior": "Anim.Idle", "clip": "asset://anim/idle.sanim" },
        { "behavior": "Anim.Reload",
          "when": [ { "request": "anim.intent.reload", "test": "param", "param": "shells", "compare": "eq", "value": 2 } ],
          "flow": "asset://anim/f.quick.flow.sdata" },
        { "behavior": "Anim.Reload", "flow": "asset://anim/f.flow.sdata" } ] })");
    (void)fx.Load("asset://anim/s.selector.sdata", kAnimSelectorType, R"({ "rules": [
        { "name": "reload", "priority": 50, "enter": [ { "request": "anim.intent.reload" } ], "behavior": "Anim.Reload" },
        { "name": "idle", "priority": 0, "enter": [], "behavior": "Anim.Idle" } ] })");
    const DataAssetHandle rig = fx.Load("asset://anim/s.rig.sdata", kAnimRigType, R"({
        "facts": "asset://anim/f.facts.sdata", "requests": "asset://anim/f.requests.sdata",
        "behaviors": [ "asset://anim/s.behaviors.sdata" ], "slot_maps": [ "asset://anim/s.slots.sdata" ],
        "layers": [ { "name": "anim.layer.base", "selector": "asset://anim/s.selector.sdata", "idle": "Anim.Idle" } ] })");
    ASSERT_TRUE(fx.Bound(rig).Valid) << AnimRigFixture::Describe(fx.Bound(rig));
    const EntityId entity = fx.Character(rig);

    const auto reload = [&](int shells) {
        AnimRequestDesc desc;
        desc.Source = entity;
        desc.Intent = fx.Tag("anim.intent.reload");
        desc.Params[0] = AnimFactFromInt(shells);
        return IssueAnimRequest(fx.Entities, entity, desc, fx.Now);
    };
    fx.Tick();
    ASSERT_TRUE(reload(1).Accepted());
    fx.Tick(20);
    EXPECT_EQ(fx.ClipName(entity, fx.Bound(rig)), "asset://anim/f.flow.sdata");

    const AnimRequestResult next = reload(2);
    ASSERT_EQ(next.Status, AnimRequestStatus::Superseded);
    fx.Tick();
    EXPECT_EQ(fx.Selection(entity).Latch, AnimLatchState::Held);
    EXPECT_EQ(fx.Selection(entity).LatchRequest, next.Id);
    EXPECT_EQ(fx.ClipName(entity, fx.Bound(rig)), "asset://anim/f.quick.flow.sdata");
    EXPECT_EQ(fx.Playing(entity).Request, next.Id);
    EXPECT_EQ(fx.Playing(entity).StartTick, fx.Last());
}
