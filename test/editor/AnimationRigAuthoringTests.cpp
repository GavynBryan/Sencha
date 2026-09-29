// A Simple rig given an upper-body reload layer through the editor's own edits,
// its new names declared by the Problems fix, and the result opened clean in a
// fresh editor. No file is written by hand.

#include "authoring/AnimationPredicateEdits.h"
#include "authoring/AnimationPreviewWorkspace.h"
#include "authoring/AnimationRigDocumentEdits.h"
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
        const AnimBoundRig* rig = workspace.Rig.Simulation.Rig();
        const std::uint16_t content = workspace.Rig.Simulation.Content()->Layers[layer].Content;
        return content < rig->Contents.size() ? rig->Contents[content].Path : std::string("(none)");
    }

    std::string FirstProblem(const AnimationPreviewWorkspace& workspace)
    {
        const std::vector<AnimDiagnostic> problems = workspace.Rig.Simulation.Problems();
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
    ASSERT_TRUE((workspace.Documents.Create(kAnimSelectorType, "animation/brute/upper.selector", error) != nullptr)) << error;
    {
        DataDocument& selector = *workspace.Documents.Find("asset://animation/brute/upper.selector.sdata");
        JsonValue root = selector.CopyRoot();
        AddAnimSelectorRule(root, "rest", "Anim.Upper.Rest", 0);
        AddAnimSelectorRule(root, "reload", "Anim.Weapon.Reload", 50);
        AddAnimPredicateRow(*AnimSelectorPredicate(root, 1, "enter"), MakeAnimRequestTest("Anim.Weapon.Reload"));
        ApplyFieldEdit(selector, workspace.Documents, FieldEdit::Instant(), std::move(root));
    }
    ASSERT_TRUE((workspace.Documents.Create(kAnimRequestSchemaType, "animation/brute/brute.requests", error) != nullptr)) << error;
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
        JsonArrayOf(data, "rows").push_back(JsonObjectOf({ { "id", JsonValue("upper_rest") }, { "behavior", JsonValue("Anim.Upper.Rest") },
                                               { "clip", JsonValue("asset://meshes/man.blend#anim:Idle") } }));
        JsonArrayOf(data, "rows").push_back(JsonObjectOf({ { "id", JsonValue("weapon_reload") }, { "behavior", JsonValue("Anim.Weapon.Reload") },
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
        DataDocument& rig = *workspace.Documents.Find(kRig);
        JsonValue root = rig.CopyRoot();
        ASSERT_TRUE(AddAnimMaskStep(root, 1, "spine", false, true));
        ApplyFieldEdit(rig, workspace.Documents, FieldEdit::Instant(), std::move(root));
    }

    // The new names are not declared yet; the Problems fix declares them.
    ASSERT_FALSE(workspace.Rig.Simulation.Rig()->Valid);
    std::vector<std::string> undeclared = UndeclaredAnimationNamesOf(workspace.Rig.Simulation, workspace.Documents);
    std::ranges::sort(undeclared);
    EXPECT_EQ(undeclared, (std::vector<std::string>{ "Anim.Upper.Rest", "Anim.Weapon.Reload" }))
        << FirstProblem(workspace);
    ASSERT_TRUE(DeclareUndeclaredAnimationNames(workspace.Documents, workspace.Rig.Simulation, workspace.Rig.Path, error)) << error;
    EXPECT_TRUE(UndeclaredAnimationNamesOf(workspace.Rig.Simulation, workspace.Documents).empty());
    const AnimBoundRig* rig = workspace.Rig.Simulation.Rig();
    ASSERT_TRUE(rig != nullptr && rig->Valid) << FirstProblem(workspace);
    ASSERT_EQ(rig->Layers.size(), 2u);
    EXPECT_FALSE(rig->Layers[1].Covers(0));
    EXPECT_TRUE(rig->Layers[1].Covers(2));
    EXPECT_EQ(workspace.Documents.Active()->VirtualPath(), kRig) << "declaring leaves the author where they were";

    // It plays: a reload on the upper body over the base's idle.
    workspace.Rig.Simulation.Step();
    AnimationScenarioAction reload;
    reload.Kind = AnimationScenarioActionKind::IssueRequest;
    reload.Participant = "player";
    reload.Intent = "Anim.Weapon.Reload";
    reload.Lifetime = AnimRequestLifetime::Impulse;
    workspace.Rig.Simulation.IssueRequest(reload);
    workspace.Rig.Simulation.Step();
    EXPECT_EQ(Playing(workspace, 0), "asset://meshes/man.blend#anim:Idle");
    EXPECT_EQ(Playing(workspace, 1), "asset://meshes/man.blend#anim:Reload");

    for (const auto& document : workspace.Documents.Documents())
        ASSERT_TRUE(!document->IsDirty() || workspace.Sources.Save(workspace.Documents.RefOf(*document)).Status == DocumentSaveStatus::Saved)
            << document->VirtualPath() << ": " << workspace.DocumentError;
    workspace.Rig.Simulation.Close();

    // A fresh editor over what was saved.
    AnimationTestProject reopened("sencha_rig_authoring", true);
    RegisterContent(reopened);
    AnimationPreviewWorkspace fresh(*reopened.Assets, {}, reopened.Root);
    ASSERT_TRUE(fresh.OpenRig(kRig)) << fresh.Rig.Error;
    const AnimBoundRig* saved = fresh.Rig.Simulation.Rig();
    ASSERT_TRUE(saved != nullptr && saved->Valid) << FirstProblem(fresh);
    EXPECT_EQ(saved->Layers.size(), 2u);
    fresh.Rig.Simulation.Close();
}
