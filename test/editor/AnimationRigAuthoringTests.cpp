// A Simple rig given an upper-body reload layer through the editor's own edits,
// its new names declared by the Problems fix, and the result opened clean in a
// fresh editor. No file is written by hand.

#include "authoring/AnimationPredicateEdits.h"
#include "authoring/AnimationPreviewWorkspace.h"
#include "authoring/AnimationRigEdits.h"
#include "authoring/AnimationRigRecipe.h"
#include "authoring/AnimationSelectorEdits.h"

#include "AnimationAuthoringSteps.h"
#include "AnimationTestProject.h"

#include <anim/AnimRequestSchema.h>
#include <anim/AnimSelectorData.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

namespace
{
    constexpr const char* kSkeleton = "asset://meshes/man.blend#skel:Man";
    constexpr const char* kRig = "asset://animation/brute/brute.rig.sdata";

    void RegisterContent(AnimationTestProject& project)
    {
        project.Skeleton(kSkeleton, { { "root", -1 }, { "spine", 0 }, { "arm", 1 } });
        project.Clip("asset://meshes/man.blend#anim:Idle", kSkeleton);
        project.Clip("asset://meshes/man.blend#anim:Walk", kSkeleton);
        project.Clip("asset://meshes/man.blend#anim:Reload", kSkeleton);
        project.ScanEngineAssets();
    }

    std::string Playing(AnimationPreviewWorkspace& workspace, std::size_t layer)
    {
        const AnimBoundRig* rig = workspace.Simulation.Rig();
        const std::uint16_t content = workspace.Simulation.Content()->Layers[layer].Content;
        return content < rig->Contents.size() ? rig->Contents[content].Path : std::string("(none)");
    }

    std::string FirstProblem(const AnimationPreviewWorkspace& workspace)
    {
        const std::vector<AnimDiagnostic> problems = workspace.Simulation.Problems();
        return problems.empty() ? std::string() : FormatAnimDiagnostic(problems.front());
    }
}

TEST(AnimationRigAuthoring, ASimpleRigGainsAnUpperBodyLayerInTheEditor)
{
    AnimationTestProject project("sencha_rig_authoring");
    RegisterContent(project);
    AnimationPreviewWorkspace workspace(*project.Assets, {}, project.Root);
    std::string error;
    ASSERT_TRUE(workspace.CreateRig({ .Name = "brute",
                                      .Clips = { "asset://meshes/man.blend#anim:Idle",
                                                 "asset://meshes/man.blend#anim:Walk" },
                                      .Preset = AnimationRigPreset::Simple, .UpperBodyJoint = {} },
                                    error))
        << error;

    // New assets, from the Document panel: the upper body's selector and the
    // request schema the reload is asked for through. The selector is edited
    // before the rig names it; the preview picks it up once the rig does.
    ASSERT_TRUE(workspace.CreateDocument(kAnimSelectorType, "animation/brute/upper.selector", error)) << error;
    {
        DataDocument& selector = *workspace.FindDocument("asset://animation/brute/upper.selector.sdata");
        JsonValue root = selector.CopyRoot();
        AddAnimSelectorRule(root, "rest", "Anim.Upper.Rest", 0);
        AddAnimSelectorRule(root, "reload", "Anim.Weapon.Reload", 50);
        AddAnimPredicateRow(*AnimSelectorPredicate(root, 1, "enter"), MakeAnimRequestTest("Anim.Weapon.Reload"));
        ApplyFieldEdit(selector, workspace, FieldEdit::Instant(), std::move(root));
    }
    ASSERT_TRUE(workspace.CreateDocument(kAnimRequestSchemaType, "animation/brute/brute.requests", error)) << error;
    AuthorDocument(workspace, "asset://animation/brute/brute.requests.sdata", [](JsonValue& data) {
        JsonArrayOf(data, "intents").push_back(JsonObjectOf({ { "intent", JsonValue("Anim.Weapon.Reload") },
                                                  { "params", JsonValue(JsonValue::Array{}) } }));
    });

    // The behaviors and their content, through the form.
    AuthorDocument(workspace, "asset://animation/brute/brute.behaviors.sdata", [](JsonValue& data) {
        JsonArrayOf(data, "behaviors").push_back(JsonObjectOf({ { "tag", JsonValue("Anim.Upper.Rest") },
                                                    { "kind", JsonValue("cyclic") } }));
        JsonArrayOf(data, "behaviors").push_back(JsonObjectOf({ { "tag", JsonValue("Anim.Weapon.Reload") },
                                                    { "kind", JsonValue("one_shot") } }));
    });
    AuthorDocument(workspace, "asset://animation/brute/brute.slots.sdata", [](JsonValue& data) {
        JsonArrayOf(data, "rows").push_back(JsonObjectOf({ { "behavior", JsonValue("Anim.Upper.Rest") },
                                               { "clip", JsonValue("asset://meshes/man.blend#anim:Idle") } }));
        JsonArrayOf(data, "rows").push_back(JsonObjectOf({ { "behavior", JsonValue("Anim.Weapon.Reload") },
                                               { "clip", JsonValue("asset://meshes/man.blend#anim:Reload") } }));
    });

    // The rig: a second layer over the base, masked from the spine.
    AuthorDocument(workspace, kRig, [](JsonValue& data) {
        data.AsObject().emplace_back("requests", JsonValue("asset://animation/brute/brute.requests.sdata"));
        JsonArrayOf(data, "layers").push_back(JsonObjectOf({ { "name", JsonValue("anim.layer.upper") },
                                                 { "selector", JsonValue("asset://animation/brute/upper.selector.sdata") },
                                                 { "idle", JsonValue("Anim.Upper.Rest") } }));
    });
    {
        DataDocument& rig = *workspace.FindDocument(kRig);
        JsonValue root = rig.CopyRoot();
        ASSERT_TRUE(AddAnimMaskStep(root, 1, "spine", false, true));
        ApplyFieldEdit(rig, workspace, FieldEdit::Instant(), std::move(root));
    }

    // The new names are not declared yet; the Problems fix declares them.
    ASSERT_FALSE(workspace.Simulation.Rig()->Valid);
    std::vector<std::string> undeclared = workspace.UndeclaredNames();
    std::ranges::sort(undeclared);
    EXPECT_EQ(undeclared, (std::vector<std::string>{ "Anim.Upper.Rest", "Anim.Weapon.Reload" }))
        << FirstProblem(workspace);
    ASSERT_TRUE(workspace.DeclareUndeclaredNames(error)) << error;
    EXPECT_TRUE(workspace.UndeclaredNames().empty());
    const AnimBoundRig* rig = workspace.Simulation.Rig();
    ASSERT_TRUE(rig != nullptr && rig->Valid) << FirstProblem(workspace);
    ASSERT_EQ(rig->Layers.size(), 2u);
    EXPECT_FALSE(rig->Layers[1].Covers(0));
    EXPECT_TRUE(rig->Layers[1].Covers(2));
    EXPECT_EQ(workspace.ActiveDocumentAny()->VirtualPath(), kRig) << "declaring leaves the author where they were";

    // It plays: a reload on the upper body over the base's idle.
    workspace.Simulation.Step();
    AnimationScenarioAction reload;
    reload.Kind = AnimationScenarioActionKind::IssueRequest;
    reload.Participant = "player";
    reload.Intent = "Anim.Weapon.Reload";
    reload.Lifetime = AnimRequestLifetime::Impulse;
    workspace.Simulation.IssueRequest(reload);
    workspace.Simulation.Step();
    EXPECT_EQ(Playing(workspace, 0), "asset://meshes/man.blend#anim:Idle");
    EXPECT_EQ(Playing(workspace, 1), "asset://meshes/man.blend#anim:Reload");

    for (const auto& document : workspace.Documents)
        ASSERT_TRUE(!document->IsDirty() || workspace.SaveDocument(*document))
            << document->VirtualPath() << ": " << workspace.DocumentError;
    workspace.Simulation.Close();

    // A fresh editor over what was saved.
    AnimationTestProject reopened("sencha_rig_authoring", true);
    RegisterContent(reopened);
    AnimationPreviewWorkspace fresh(*reopened.Assets, {}, reopened.Root);
    ASSERT_TRUE(fresh.OpenRig(kRig)) << fresh.ScenarioError;
    const AnimBoundRig* saved = fresh.Simulation.Rig();
    ASSERT_TRUE(saved != nullptr && saved->Valid) << FirstProblem(fresh);
    EXPECT_EQ(saved->Layers.size(), 2u);
    fresh.Simulation.Close();
}
