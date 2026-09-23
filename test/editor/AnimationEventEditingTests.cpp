// Authoring clip events without a GUI: a marker placed on a clip, bound, fed
// and played through in the preview, the way the event panels do it -- no
// game, and nothing written to disk until saved.

#include "authoring/AnimationEventBindings.h"
#include "authoring/AnimationPreviewWorkspace.h"

#include <anim/AnimationClipCache.h>
#include <assets/runtime/RuntimeAssets.h>
#include <authored/VerbRegistry.h>
#include <authored/WorldVocabulary.h>
#include <core/assets/AssetRegistry.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

namespace
{
    constexpr std::string_view kClip = "asset://anim/hero.glb#anim:Walk";

    DataFieldSchema Arg(std::string key, DataFieldKind kind)
    {
        DataFieldSchema field;
        field.Key = std::move(key);
        field.Kind = kind;
        return field;
    }

    // What the project's module would declare.
    void Vocabulary(World& world)
    {
        for (const char* tag : { "Anim.Walk", "Surface.Grass" })
            (void)world.GetResource<GameplayTagRegistry>().RegisterTag(tag);
        VerbRegistrationScope scope(InstallVerbRegistry(world), "test");
        VerbDefinition footstep;
        footstep.Name = "test.footstep";
        footstep.Arguments.Children = { Arg("Surface", DataFieldKind::GameplayTag), Arg("Volume", DataFieldKind::Float) };
        (void)scope.Declare(std::move(footstep));
        VerbDefinition count;
        count.Name = "test.count";
        count.Arguments.Children = { Arg("Amount", DataFieldKind::Int), Arg("Scale", DataFieldKind::Float) };
        (void)scope.Declare(std::move(count));
        (void)scope.Commit();
    }

    // A project on disk: a walking rig whose clip is cooked from hero.glb, so
    // its events live in hero.glb.meta beside the source.
    struct EventProject
    {
        std::filesystem::path Root = std::filesystem::temp_directory_path()
            / ("sencha_event_editing_" + std::to_string(std::random_device{}()));
        LoggingProvider Logging;
        ComponentSerializerRegistry Serializers;
        std::unique_ptr<RuntimeAssets> Assets;
        AssetLease ClipLease;

        EventProject()
        {
            std::filesystem::create_directories(Root / "anim");
            Write("hero.bindings.sdata", R"({ "type": "authored.bindings", "version": 1, "data": { "bindings": [
                { "key": "anim.footstep", "verb": "test.footstep", "inputs": [ "surface", "volume" ],
                  "arguments": { "Surface": { "input": "surface" }, "Volume": { "input": "volume" } } },
                { "key": "anim.count", "verb": "test.count", "inputs": [ "amount" ],
                  "arguments": { "Amount": { "input": "amount" }, "Scale": { "input": "amount" } } } ] } })");
            Write("hero.behaviors.sdata", R"({ "type": "animation.behavior_set", "version": 1, "data": {
                "behaviors": [ { "tag": "Anim.Walk", "kind": "cyclic" } ] } })");
            Write("hero.slots.sdata", R"({ "type": "animation.slot_map", "version": 1, "data": { "rows": [
                { "behavior": "Anim.Walk", "clip": "asset://anim/hero.glb#anim:Walk" } ] } })");
            Write("hero.rig.sdata", R"({ "type": "animation.rig", "version": 1, "data": {
                "behaviors": [ "asset://anim/hero.behaviors.sdata" ], "slot_maps": [ "asset://anim/hero.slots.sdata" ],
                "bindings": [ "asset://anim/hero.bindings.sdata" ],
                "layers": [ { "name": "anim.layer.base", "idle": "Anim.Walk" } ] } })");
            Write("hero.rig.sanimscenario", R"({ "type": "animation.preview_scenario", "version": 1,
                "name": "walk", "rig": "asset://anim/hero.rig.sdata", "participants": [ "player" ],
                "recorders": [ "test.footstep" ] })");

            Assets = std::make_unique<RuntimeAssets>(Logging, Serializers);
            EXPECT_TRUE(Assets->Registry.RegisterOrVerify(AssetRecord{
                .Type = AssetType::AnimationClip,
                .SourceKind = AssetSourceKind::Procedural,
                .Path = std::string(kClip),
                .FilePath = (Root / ".cooked" / "anim" / "hero.glb.anim:Walk.sanim").generic_string() }));
            AnimationClipData clip;
            clip.DurationSeconds = 1.0f;
            (void)Assets->AnimationClips.Register(kClip, std::move(clip), {});
            ClipLease = Assets->Assets.TryAcquireLease(kClip, AssetType::AnimationClip);
            (void)ScanAssetsDirectory(Root.generic_string(), Assets->Registry, Assets->Assets.Kinds());
        }

        ~EventProject()
        {
            ClipLease = {};
            Assets.reset();
            std::error_code ec;
            std::filesystem::remove_all(Root, ec);
        }

        void Write(const std::string& name, std::string_view text) { std::ofstream(Root / "anim" / name) << text; }
        bool Exists(const std::string& name) const { return std::filesystem::exists(Root / "anim" / name); }
    };

    AnimationClipEvent Footstep(float time)
    {
        AnimationClipEvent event;
        event.Time = time;
        event.Binding = "anim.footstep";
        VerbBindingArgument surface;
        surface.Key = "surface";
        surface.Source = VerbArgumentSource::Tag;
        surface.Text = "Surface.Grass";
        VerbBindingArgument volume;
        volume.Key = "volume";
        volume.Literal = JsonValue(0.5);
        event.Inputs = { surface, volume };
        return event;
    }

    const AnimDecisionRecord* LastCrossing(const AnimationPreviewSession& session)
    {
        const AnimDecisionRecord* last = nullptr;
        for (const AnimationPreviewTickRecord& tick : session.History())
            for (const AnimDecisionRecord& record : tick.Decisions)
                if (record.Cause == AnimDecisionCause::EventCrossed)
                    last = &record;
        return last;
    }
}

// Place a marker, choose its binding, supply its inputs, play through it: the
// admission is the normal one, and the file is untouched until saved.
TEST(AnimationEventEditing, AMarkerPlayedThroughShowsItsAdmission)
{
    EventProject project;
    AnimationPreviewWorkspace workspace(*project.Assets, &Vocabulary);
    ASSERT_TRUE(workspace.OpenRig("asset://anim/hero.rig.sdata")) << workspace.ScenarioError;
    ASSERT_TRUE(workspace.OpenClipEvents(std::string(kClip))) << workspace.DocumentError;
    AnimationClipEventsDocument* events = workspace.FindClipEvents(kClip);
    ASSERT_NE(events, nullptr);
    EXPECT_EQ(events->SidecarPath(), project.Root / "anim" / "hero.glb.meta");

    const std::uint32_t key = events->Add(Footstep(0.25f));
    workspace.ClipEventsChanged(*events);
    EXPECT_TRUE(workspace.PreviewStatus[std::string(kClip)].empty()) << workspace.PreviewStatus[std::string(kClip)];

    workspace.Simulation.RunTo(30);
    const AnimDecisionRecord* crossing = LastCrossing(workspace.Simulation);
    ASSERT_NE(crossing, nullptr);
    EXPECT_EQ(crossing->EventKey, key);
    EXPECT_EQ(crossing->Admission, VerbAdmission::Accepted);
    EXPECT_FALSE(project.Exists("hero.glb.meta")) << "nothing is written until saved";

    ASSERT_TRUE(workspace.SaveClipEvents(*events)) << workspace.DocumentError;
    EXPECT_TRUE(project.Exists("hero.glb.meta"));
}

// Escape during a drag puts the marker back, in the document and in the
// preview's clip.
TEST(AnimationEventEditing, CancellingADragRestoresThePreviewToo)
{
    EventProject project;
    AnimationPreviewWorkspace workspace(*project.Assets, &Vocabulary);
    ASSERT_TRUE(workspace.OpenRig("asset://anim/hero.rig.sdata"));
    ASSERT_TRUE(workspace.OpenClipEvents(std::string(kClip)));
    AnimationClipEventsDocument& events = *workspace.FindClipEvents(kClip);
    const std::uint32_t key = events.Add(Footstep(0.25f));
    workspace.ClipEventsChanged(events);

    events.BeginEdit(key);
    AnimationClipEvent moved = *events.Find(key);
    moved.Time = 0.8f;
    events.PreviewEdit(moved);
    workspace.ClipEventsChanged(events);
    const AnimationClipData* clip = project.Assets->AnimationClips.Get(project.Assets->AnimationClips.Find(kClip));
    ASSERT_EQ(clip->Events.size(), 1u);
    EXPECT_FLOAT_EQ(clip->Events[0].Time, 0.8f) << "the preview follows the drag";

    workspace.CancelAuthoringEdit();
    EXPECT_FALSE(events.IsEditing());
    EXPECT_FLOAT_EQ(events.Find(key)->Time, 0.25f);
    EXPECT_FLOAT_EQ(clip->Events[0].Time, 0.25f);
}

// A binding created from a declared verb takes an input per argument, and an
// event naming it resolves once the rig rebinds.
TEST(AnimationEventEditing, ACreatedBindingIsOneTheEventsCanName)
{
    EventProject project;
    AnimationPreviewWorkspace workspace(*project.Assets, &Vocabulary);
    ASSERT_TRUE(workspace.OpenRig("asset://anim/hero.rig.sdata"));
    ASSERT_TRUE(workspace.CreateBinding("asset://anim/hero.bindings.sdata", "anim.footstep_loud", "test.footstep"))
        << workspace.DocumentError;
    EXPECT_FALSE(workspace.CreateBinding("asset://anim/hero.bindings.sdata", "anim.footstep", "test.footstep"));

    workspace.Simulation.Rebind();
    const AnimBoundRig* rig = workspace.Simulation.Rig();
    ASSERT_NE(rig, nullptr);
    const std::vector<AnimationBindingView> views = DescribeAnimationBindings(*rig, *workspace.Simulation.Verbs());
    const auto created = std::ranges::find(views, std::string("anim.footstep_loud"), &AnimationBindingView::Key);
    ASSERT_NE(created, views.end());
    EXPECT_EQ(created->Verb, "test.footstep");
    ASSERT_EQ(created->Inputs.size(), 2u);
    EXPECT_EQ(created->Inputs[0].Name, "Surface");
    EXPECT_EQ(created->Inputs[0].Destinations.at(0).second, DataFieldKind::GameplayTag);

    ASSERT_TRUE(workspace.OpenClipEvents(std::string(kClip)));
    AnimationClipEventsDocument& events = *workspace.FindClipEvents(kClip);
    AnimationClipEvent loud = Footstep(0.5f);
    loud.Binding = "anim.footstep_loud";
    loud.Inputs[0].Key = "Surface";
    loud.Inputs[1].Key = "Volume";
    (void)events.Add(loud);
    workspace.ClipEventsChanged(events);
    ASSERT_EQ(workspace.Simulation.Rig()->Contents.at(0).Events.size(), 1u);
    EXPECT_TRUE(workspace.Simulation.Rig()->Contents[0].Events[0].Resolved)
        << (workspace.Simulation.Rig()->Diagnostics.empty() ? "" : workspace.Simulation.Rig()->Diagnostics.back().Message);
}

// The inspector's check is the binding's: one input filling an Int and a
// Float argument must suit both.
TEST(AnimationEventEditing, AnInputIsCheckedAgainstEveryDestination)
{
    EventProject project;
    AnimationPreviewWorkspace workspace(*project.Assets, &Vocabulary);
    ASSERT_TRUE(workspace.OpenRig("asset://anim/hero.rig.sdata"));
    const CompiledVerbBinding* count = workspace.Simulation.Rig()->Bindings.Find("anim.count");
    ASSERT_NE(count, nullptr);
    VerbBindingEnvironment environment{ .Verbs = workspace.Simulation.Verbs(), .Tags = workspace.Simulation.Tags() };

    AnimationClipEvent event;
    event.Binding = "anim.count";
    VerbBindingArgument amount;
    amount.Key = "amount";
    amount.Literal = JsonValue(2.5);
    event.Inputs = { amount };
    std::vector<AnimationEventInputCheck> checks = CheckAnimationEventInputs(event, *count, environment);
    ASSERT_EQ(checks.size(), 1u);
    EXPECT_FALSE(checks[0].Valid);
    EXPECT_FALSE(checks[0].Message.empty());

    event.Inputs[0].Key = "amout";
    checks = CheckAnimationEventInputs(event, *count, environment);
    ASSERT_EQ(checks.size(), 2u);
    EXPECT_FALSE(checks[0].Supplied);
    EXPECT_EQ(checks[1].Input, "amout");
    EXPECT_FALSE(checks[1].Valid);
}
