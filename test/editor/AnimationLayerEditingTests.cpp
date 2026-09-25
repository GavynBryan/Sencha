// Headless layer and flow editing: mask steps apply in the rig document's
// transaction and rebind the running preview without writing to disk, and flow
// structure edits cannot make control go backward.

#include "authoring/AnimationFlowEdits.h"
#include "authoring/AnimationPredicateEdits.h"
#include "authoring/AnimationPreviewWorkspace.h"
#include "authoring/AnimationRigEdits.h"

#include <anim/AnimFlowData.h>
#include <anim/AnimRigData.h>
#include <anim/AnimationClipCache.h>
#include <anim/SkeletonCache.h>
#include <assets/data/DataAssetTypeRegistry.h>
#include <assets/runtime/RuntimeAssets.h>
#include <core/assets/AssetRegistry.h>
#include <core/json/JsonParser.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>

namespace
{
    constexpr const char* kSkeleton = "asset://anim/biped.sskel";

    // root > pelvis > spine > head; spine > arm.
    SkeletonData Biped()
    {
        SkeletonData skeleton;
        for (const auto& [name, parent] : { std::pair{ "root", -1 }, std::pair{ "pelvis", 0 },
                                            std::pair{ "spine", 1 }, std::pair{ "head", 2 }, std::pair{ "arm", 2 } })
        {
            SkeletonJoint joint;
            joint.Name = name;
            joint.ParentIndex = parent;
            skeleton.Joints.push_back(joint);
        }
        return skeleton;
    }

    struct Project
    {
        std::filesystem::path Root = std::filesystem::temp_directory_path() / "sencha_layer_editing";
        LoggingProvider Logging;
        ComponentSerializerRegistry Serializers;
        std::unique_ptr<RuntimeAssets> Assets;
        std::vector<AssetLease> Leases;

        Project()
        {
            std::filesystem::remove_all(Root);
            std::filesystem::create_directories(Root / "anim");
            Write("hero.behaviors.sdata", R"({ "type": "animation.behavior_set", "version": 1, "data": {
                "behaviors": [ { "tag": "Anim.Idle", "kind": "cyclic" } ] } })");
            Write("hero.slots.sdata", R"({ "type": "animation.slot_map", "version": 1, "data": { "rows": [
                { "behavior": "Anim.Idle", "clip": "asset://anim/idle.sanim" } ] } })");
            Write("hero.rig.sdata", R"({ "type": "animation.rig", "version": 1, "data": {
                "skeleton": "asset://anim/biped.sskel",
                "behaviors": [ "asset://anim/hero.behaviors.sdata" ], "slot_maps": [ "asset://anim/hero.slots.sdata" ],
                "layers": [ { "name": "anim.layer.base", "idle": "Anim.Idle" },
                            { "name": "anim.layer.upper", "idle": "Anim.Idle", "mode": "additive" } ] } })");
            Write("hero.rig.sanimscenario", R"({ "type": "animation.preview_scenario", "version": 1,
                "name": "stand", "rig": "asset://anim/hero.rig.sdata", "participants": [ "player" ],
                "declared_tags": [ "Anim.Idle", "anim.layer.upper" ] })");

            Assets = std::make_unique<RuntimeAssets>(Logging, Serializers);
            EXPECT_TRUE(Assets->Registry.RegisterOrVerify(AssetRecord{
                .Type = AssetType::Skeleton, .SourceKind = AssetSourceKind::Procedural, .Path = kSkeleton }));
            (void)Assets->Skeletons.Register(kSkeleton, Biped());
            Leases.push_back(Assets->Assets.TryAcquireLease(kSkeleton, AssetType::Skeleton));
            EXPECT_TRUE(Assets->Registry.RegisterOrVerify(AssetRecord{ .Type = AssetType::AnimationClip,
                                                                       .SourceKind = AssetSourceKind::Procedural,
                                                                       .Path = "asset://anim/idle.sanim" }));
            AnimationClipData idle;
            idle.DurationSeconds = 1.0f;
            (void)Assets->AnimationClips.Register("asset://anim/idle.sanim", std::move(idle),
                                                  Assets->Skeletons.AcquireOwned(kSkeleton));
            Leases.push_back(Assets->Assets.TryAcquireLease("asset://anim/idle.sanim", AssetType::AnimationClip));
            (void)ScanAssetsDirectory(Root.generic_string(), Assets->Registry, Assets->Assets.Kinds());
        }

        ~Project()
        {
            Leases.clear();
            std::filesystem::remove_all(Root);
        }

        void Write(const std::string& name, std::string_view text) { std::ofstream(Root / "anim" / name) << text; }

        std::string Read(const std::string& name) const
        {
            std::ifstream in(Root / "anim" / name);
            std::stringstream text;
            text << in.rdbuf();
            return text.str();
        }
    };

    void Apply(AnimationPreviewWorkspace& workspace, DataDocument& rig, const std::function<bool(JsonValue&)>& edit)
    {
        JsonValue root = rig.CopyRoot();
        ASSERT_TRUE(edit(root));
        rig.BeginEdit();
        rig.PreviewRoot(std::move(root));
        workspace.CommitDocumentEdit(rig);
    }
}

TEST(AnimationLayerEditing, AMaskStepRebindsTheRunningPreview)
{
    Project project;
    {
        AnimationPreviewWorkspace workspace(*project.Assets);
        ASSERT_TRUE(workspace.OpenRig("asset://anim/hero.rig.sdata")) << workspace.ScenarioError;
        ASSERT_NE(workspace.Simulation.Rig(), nullptr);
        ASSERT_TRUE(workspace.Simulation.Rig()->Valid);
        ASSERT_NE(workspace.RigSkeleton(), nullptr);
        EXPECT_FALSE(workspace.Simulation.Rig()->Layers[1].Masked());

        ASSERT_TRUE(workspace.OpenAnimationDocument("asset://anim/hero.rig.sdata"));
        DataDocument* rig = workspace.ActiveDocumentOf(kAnimRigType);
        ASSERT_NE(rig, nullptr);
        const std::string saved = project.Read("hero.rig.sdata");

        Apply(workspace, *rig, [](JsonValue& root) { return AddAnimMaskStep(root, 1, "spine", false, true); });
        Apply(workspace, *rig, [](JsonValue& root) { return AddAnimMaskStep(root, 1, "head", true, false); });
        workspace.Simulation.Step();
        const AnimBoundRig* bound = workspace.Simulation.Rig();
        ASSERT_TRUE(bound->Valid);
        EXPECT_EQ(bound->Layers[1].Mask, (std::vector<std::uint8_t>{ 0, 0, 1, 0, 1 }));
        EXPECT_EQ(AnimMaskCoverage(*bound), (std::vector<std::uint8_t>{ 0b01, 0b01, 0b11, 0b01, 0b11 }));

        // A joint the skeleton lacks is a document that compiles and a rig
        // that does not bind, and the problem names the step.
        Apply(workspace, *rig, [](JsonValue& root) { return AddAnimMaskStep(root, 1, "tail", false, true); });
        workspace.Simulation.Step();
        bound = workspace.Simulation.Rig();
        EXPECT_FALSE(bound->Valid);
        ASSERT_FALSE(bound->Diagnostics.empty());
        EXPECT_EQ(bound->Diagnostics.front().Code, "anim.mask.joint_unknown");
        EXPECT_EQ(bound->Diagnostics.front().FieldPath, "$.data.layers[1].mask[2].joint");

        // Undo each step; the last removes the mask entirely.
        for (int i = 0; i < 3; ++i)
        {
            rig->Undo();
            workspace.DocumentChanged(*rig);
        }
        workspace.Simulation.Step();
        EXPECT_FALSE(workspace.Simulation.Rig()->Layers[1].Masked());
        EXPECT_EQ(project.Read("hero.rig.sdata"), saved);
        workspace.Simulation.Close();
    }
}

TEST(AnimationLayerEditing, RemovingTheLastStepRemovesTheMask)
{
    std::optional<JsonValue> root = JsonParse(R"({ "data": { "layers": [ { "name": "a" }, { "name": "b" } ] } })");
    ASSERT_TRUE(root.has_value());
    ASSERT_TRUE(AddAnimMaskStep(*root, 1, "spine", false, true));
    ASSERT_TRUE(AddAnimMaskStep(*root, 1, "head", true, false));
    const JsonValue& step = (*AnimRigLayers(*root))[1].Find("mask")->AsArray()[1];
    EXPECT_TRUE(step.Find("exclude")->AsBool());
    EXPECT_FALSE(step.Find("subtree")->AsBool());
    EXPECT_EQ((*AnimRigLayers(*root))[1].Find("mask")->AsArray()[0].AsObject().size(), 1u)
        << "defaults are not written";
    ASSERT_TRUE(RemoveAnimMaskStep(*root, 1, 0));
    ASSERT_TRUE(RemoveAnimMaskStep(*root, 1, 0));
    EXPECT_EQ((*AnimRigLayers(*root))[1].Find("mask"), nullptr);
    EXPECT_FALSE(AddAnimMaskStep(*root, 5, "spine", false, true));
}

// Flow structure edits as the Flow panel applies them: every edit leaves a
// flow that compiles, and none can make control go backward.
TEST(AnimationFlowEdits, NoEditMakesControlGoBackward)
{
    DataAssetTypeRegistry types;
    DataSchemaRegistry schemas;
    RegisterAnimFlowData(types, schemas);
    std::optional<JsonValue> root = JsonParse(R"({ "type": "animation.flow", "version": 1, "data": { "sections": [
        { "tag": "Anim.A", "clip": "asset://anim/a.sanim" } ] } })");
    ASSERT_TRUE(root.has_value());
    const auto compiles = [&] {
        const DataAssetCompileResult result = types.Find(kAnimFlowType)->Compile(*root->Find("data"));
        EXPECT_TRUE(result.IsValid()) << result.Error;
        return result.IsValid() ? std::static_pointer_cast<const AnimFlowData>(result.Value) : nullptr;
    };

    AddAnimFlowSection(*root, "Anim.B", "asset://anim/b.sanim");
    AddAnimFlowSection(*root, "Anim.C", "asset://anim/c.sanim");
    ASSERT_TRUE(AddAnimFlowBranch(*root, 0, 2));
    EXPECT_FALSE(AddAnimFlowBranch(*root, 2, 0)) << "a branch goes only to a later section";
    EXPECT_FALSE(AddAnimFlowBranch(*root, 1, 1));
    ASSERT_TRUE(SetAnimFlowCancel(*root, "Anim.C"));
    ASSERT_TRUE(SetAnimFlowEnds(*root, 1, true));
    ASSERT_TRUE(SetAnimFlowCancelImmediately(*root, 1, true));
    ASSERT_TRUE(SetAnimFlowLoop(*root, 1, "count"));
    (*AnimFlowSections(*root))[1].AsObject().emplace_back("count_intent", JsonValue("anim.intent.reload"));
    (*AnimFlowSections(*root))[1].AsObject().emplace_back("count_param", JsonValue("shells"));
    std::shared_ptr<const AnimFlowData> flow = compiles();
    ASSERT_NE(flow, nullptr);
    EXPECT_EQ(flow->Sections[0].Branches[0].To, "Anim.C");
    EXPECT_EQ(flow->Cancel, "Anim.C");
    EXPECT_TRUE(flow->Sections[1].Ends);
    EXPECT_EQ(flow->Sections[1].CancelTiming, AnimCancelTiming::Immediate);
    EXPECT_EQ(flow->Sections[1].Loop, AnimFlowLoop::Count);

    // Moving C above A would turn A's branch backward.
    EXPECT_FALSE(MoveAnimFlowSection(*root, 2, 0));
    EXPECT_TRUE(MoveAnimFlowSection(*root, 1, 2));
    ASSERT_NE(compiles(), nullptr);

    // Back to once drops the count's fields; a removed section takes its
    // branches and the cancel with it.
    ASSERT_TRUE(SetAnimFlowLoop(*root, 2, "once"));
    EXPECT_EQ((*AnimFlowSections(*root))[2].Find("count_param"), nullptr);
    ASSERT_TRUE(RemoveAnimFlowSection(*root, 1));
    flow = compiles();
    ASSERT_NE(flow, nullptr);
    EXPECT_TRUE(flow->Sections[0].Branches.empty());
    EXPECT_TRUE(flow->Cancel.empty());
}

// Conditions edit with the same predicate edits the rule table uses, over the
// rows a while loop or a branch holds.
TEST(AnimationFlowEdits, LoopAndBranchConditionsEditAsPredicates)
{
    DataAssetTypeRegistry types;
    DataSchemaRegistry schemas;
    RegisterAnimFlowData(types, schemas);
    std::optional<JsonValue> root = JsonParse(R"({ "type": "animation.flow", "version": 1, "data": { "sections": [
        { "tag": "Anim.A", "clip": "asset://anim/a.sanim" }, { "tag": "Anim.B", "clip": "asset://anim/b.sanim" } ] } })");
    ASSERT_TRUE(root.has_value());

    EXPECT_EQ(AnimFlowLoopCondition(*root, 0), nullptr) << "a section that plays once has no loop condition";
    ASSERT_TRUE(SetAnimFlowLoop(*root, 0, "while"));
    JsonValue::Array* loop = AnimFlowLoopCondition(*root, 0);
    ASSERT_NE(loop, nullptr);
    AddAnimPredicateRow(*loop, MakeAnimRequestTest("anim.intent.charge"));
    AddAnimPredicateRow(*loop, MakeAnimFactTest("Speed", AnimFactKind::Float));
    ASSERT_TRUE(AddAnimPredicateAlternative(*loop, 1, MakeAnimFactTest("Crouched", AnimFactKind::Bool)));

    ASSERT_TRUE(AddAnimFlowBranch(*root, 0, 1));
    JsonValue::Array* when = AnimFlowBranchCondition(*root, 0, 0);
    ASSERT_NE(when, nullptr);
    JsonValue released = MakeAnimRequestTest("anim.intent.charge");
    released.AsObject().emplace_back("not", JsonValue(true));
    AddAnimPredicateRow(*when, std::move(released));
    EXPECT_EQ(AnimFlowBranchCondition(*root, 0, 3), nullptr);

    const DataAssetCompileResult result = types.Find(kAnimFlowType)->Compile(*root->Find("data"));
    ASSERT_TRUE(result.IsValid()) << result.Error;
    const auto flow = std::static_pointer_cast<const AnimFlowData>(result.Value);
    ASSERT_EQ(flow->Sections[0].While.Rows.size(), 2u);
    EXPECT_EQ(flow->Sections[0].While.Rows[1].AnyOf.size(), 2u);
    ASSERT_EQ(flow->Sections[0].Branches[0].When.Rows.size(), 1u);
    EXPECT_TRUE(flow->Sections[0].Branches[0].When.Rows[0].AnyOf.front().Negate);
}
