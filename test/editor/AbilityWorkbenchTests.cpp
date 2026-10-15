// The Workbench over the project's documents: an edit is one step of the
// journal, a selection follows its element through reordering and undo, a
// problem is revealed as its element selected with its field brought forward,
// and the inspector shows only the selected element.

#include "authoring/AbilityArguments.h"
#include "authoring/AbilityDocumentEdits.h"
#include "authoring/AbilityOverview.h"
#include "authoring/AbilityProblemProperties.h"
#include "authoring/AbilityWorkbench.h"
#include "data/DataDocument.h"
#include "data/DataPath.h"
#include "data/DataProperties.h"
#include "data/JsonObjectEdit.h"
#include "documents/DocumentSourceSet.h"
#include "presenters/PropertySheet.h"

#include <assets/runtime/RuntimeAssets.h>
#include <effects/EffectDefinitionData.h>
#include <core/assets/AssetRegistry.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <core/json/JsonParser.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <random>

namespace
{
constexpr std::string_view kCharge = "asset://abilities/charge_release.sdata";

struct AbilityWorkbenchTest : testing::Test
{
    std::filesystem::path Root = std::filesystem::temp_directory_path()
        / ("sencha_ability_workbench_" + std::to_string(std::random_device{}()));
    LoggingProvider Logging;
    ComponentSerializerRegistry Serializers;
    RuntimeAssets Assets{ Logging, Serializers };
    DocumentSourceSet Sources;
    DataDocumentStore Store{ Assets, Sources };
    std::unique_ptr<AbilityWorkbench> Bench;

    void SetUp() override
    {
        std::filesystem::copy(std::filesystem::path(SENCHA_REPO_ROOT) / "test/fixtures/abilities/assets", Root,
                              std::filesystem::copy_options::recursive);
        (void)ScanAssetsDirectory(Root.generic_string(), Assets.Registry, Assets.Assets.Kinds());
        Bench = std::make_unique<AbilityWorkbench>(Assets, Store, std::vector<std::filesystem::path>{ Root },
                                                   std::function<void(VocabularyCatalog&)>{});
        std::string error;
        ASSERT_TRUE(Bench->Open(kCharge, error)) << error;
    }

    void TearDown() override
    {
        Sources.DiscardAll();
        Bench.reset();
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }

    [[nodiscard]] const JsonValue& Data() const { return *Bench->AbilityData(); }

    [[nodiscard]] const JsonValue& ArgumentOf(std::string_view op, std::string_view argument) const
    {
        const std::optional<AbilityElement> element = FindAbilityElement(Data(), op);
        return *FindMember(*FindMember(*FindDataValue(Data(), element->Path), "arguments"), argument);
    }
};

const PropertyNode* Find(const std::vector<PropertyNode>& nodes, std::string_view key)
{
    for (const PropertyNode& node : nodes)
    {
        if (node.Key == key)
            return &node;
        if (const PropertyNode* inside = Find(node.Children, key))
            return inside;
    }
    return nullptr;
}
}

TEST_F(AbilityWorkbenchTest, TheNormativeChargeHasNoProblems)
{
    for (const AbilityDiagnostic& diagnostic : Bench->Diagnostics())
        EXPECT_NE(diagnostic.Severity, AbilityDiagnosticSeverity::Error) << diagnostic.Code << " " << diagnostic.Message;
    ASSERT_EQ(Bench->Abilities().size(), 1u);
    EXPECT_EQ(Bench->Abilities().front().AssetPath, kCharge);
}

TEST_F(AbilityWorkbenchTest, AnEditIsOneStepThatUndoesAndRedoes)
{
    std::string added;
    ASSERT_TRUE(Bench->Edit([&](JsonValue& data) {
        added = AddAbilityPhase(data, "Aim", "p_charge");
        return true;
    }));
    Bench->Select(added);
    ASSERT_TRUE(FindAbilityElement(Data(), added));
    Sources.Undo();
    EXPECT_FALSE(FindAbilityElement(Data(), added));
    EXPECT_TRUE(Bench->Selected().empty()) << "the selection let go of what is no longer there";
    Sources.Redo();
    EXPECT_TRUE(FindAbilityElement(Data(), added));
    EXPECT_FALSE(Bench->Edit([](JsonValue&) { return false; })) << "a refused change is no step";
    EXPECT_TRUE(Sources.CanRedo() == false);
}

TEST_F(AbilityWorkbenchTest, TheSelectionFollowsItsElementThroughAReorder)
{
    Bench->Select("o_trace");
    const std::string before = Bench->SelectedElement()->Path;
    ASSERT_TRUE(Bench->Edit([](JsonValue& data) { return MoveAbilityPhase(data, "p_fire", 0); }));
    const std::optional<AbilityElement> selected = Bench->SelectedElement();
    ASSERT_TRUE(selected);
    EXPECT_EQ(selected->Key, "o_trace");
    EXPECT_NE(selected->Path, before);
    EXPECT_EQ(selected->Path, "$.data.phases[0].enter[1]");
}

TEST_F(AbilityWorkbenchTest, AProblemIsRevealedAsItsElementWithItsFieldForward)
{
    ASSERT_TRUE(Bench->Edit([](JsonValue& data) {
        return SetAbilityElementMember(data, "o_damage", "verb", JsonValue("effect.aply"));
    }));
    const std::vector<Problem> problems = Bench->Problems();
    const auto unknown = std::ranges::find_if(problems, [](const Problem& problem) {
        return problem.Message.find("effect.aply") != std::string::npos;
    });
    ASSERT_NE(unknown, problems.end());
    EXPECT_EQ(unknown->Severity, ProblemSeverity::Error);
    Bench->Reveal(unknown->Location);
    EXPECT_EQ(Bench->Selected(), "o_damage");
    EXPECT_EQ(Bench->Focus(), unknown->Location);
}

TEST_F(AbilityWorkbenchTest, TheInspectorShowsOnlyTheSelectedElementAndFocusesItsField)
{
    DataProperties fields(Bench->Documents, [this] { return Bench->Ability(); }, nullptr, [this] {
        const std::optional<AbilityElement> element = Bench->SelectedElement();
        return element ? element->Path : std::string{};
    });
    Bench->Select("p_recover", "$.data.phases[2].then[0].ticks");
    const std::vector<PropertyNode> nodes = fields.Nodes();
    ASSERT_EQ(nodes.size(), 1u);
    EXPECT_EQ(nodes.front().Key, "$.data.phases[2]");
    EXPECT_EQ(nodes.front().Descriptor.Label, "Recover");

    PropertySheet sheet;
    sheet.Focus(Bench->Focus());
    const std::vector<UiRow> rows = sheet.Present(fields);
    ASSERT_TRUE(sheet.FocusedRow().has_value()) << "the field is shown, its groups opened";
    EXPECT_EQ(rows[*sheet.FocusedRow()].Key, "$.data.phases[2].then[0].ticks");
    EXPECT_TRUE(rows[*sheet.FocusedRow()].Selected);

    ASSERT_EQ(fields.CommitEdit("$.data.phases[2].then[0].ticks", "30"), PropertyEditResult::Applied);
    EXPECT_EQ(Data().Find("phases")->AsArray()[2].Find("then")->AsArray()[0].Find("ticks")->AsNumber(), 30.0)
        << "an edit through the scoped element is made by its whole path";
}

TEST_F(AbilityWorkbenchTest, AnOpsArgumentsAreLaidOutFromItsContractAndSetAsSteps)
{
    AbilityArgumentProperties arguments(*Bench, {});
    Bench->Select("o_trace");
    const std::vector<PropertyNode> nodes = arguments.Nodes();
    const PropertyNode* range = Find(nodes, "arg.range.value");
    ASSERT_NE(range, nullptr) << "physics.trace declares a range";
    EXPECT_EQ(range->Value, "80");
    EXPECT_EQ(Find(nodes, "arg.origin.source")->Value, "Port");
    EXPECT_EQ(Find(nodes, "arg.origin.value")->Value, "aim.origin");

    ASSERT_EQ(arguments.CommitEdit("arg.range.value", "40"), PropertyEditResult::Applied);
    EXPECT_EQ(FindMember(ArgumentOf("o_trace", "range"), "const")->AsNumber(), 40.0);
    EXPECT_EQ(arguments.CommitEdit("arg.range.value", "far"), PropertyEditResult::Refused);
    ASSERT_EQ(arguments.CommitEdit("arg.origin.source", "Local"), PropertyEditResult::Applied);
    ASSERT_EQ(arguments.CommitEdit("arg.origin.value", "Hit.point"), PropertyEditResult::Applied);
    EXPECT_EQ(ReadMemberString(ArgumentOf("o_trace", "origin"), "local"), "l_hit");
    EXPECT_EQ(ReadMemberString(ArgumentOf("o_trace", "origin"), "field"), "point");
    Sources.Undo();
    EXPECT_EQ(ReadMemberString(ArgumentOf("o_trace", "origin"), "local"), "l_charge")
        << "one step each: back to the first local the switch to a local took";
}

TEST_F(AbilityWorkbenchTest, AnUnguardedRecipientIsFixedWithOneStep)
{
    ASSERT_TRUE(Bench->Edit([](JsonValue& data) { return SetAbilityGuard(data, "o_damage", JsonValue()).empty(); }));
    Bench->Select("o_damage");
    AbilityProblemProperties problems(*Bench);
    const std::vector<PropertyNode> nodes = problems.Nodes();
    const auto fixable = std::ranges::find_if(nodes, [](const PropertyNode& node) { return !node.Descriptor.Verbs.empty(); });
    ASSERT_NE(fixable, nodes.end()) << "the recipient problem offers its guard";
    EXPECT_EQ(fixable->Descriptor.Verbs.front().Label, "Guard with 'entity(Hit.entity) has sencha.attribute_set'");
    ASSERT_EQ(problems.Act(fixable->Key, "fix", {}), PropertyEditResult::Applied);
    for (const AbilityDiagnostic& diagnostic : Bench->Diagnostics())
        EXPECT_NE(diagnostic.Code, "unguarded_recipient") << diagnostic.Message;
}

TEST_F(AbilityWorkbenchTest, AFixJoinsTheGuardAnOpAlreadyHas)
{
    ASSERT_TRUE(Bench->Edit([](JsonValue& data) {
        return !SetAbilityGuard(data, "o_damage", *JsonParse(R"({ "input": "held" })")).empty();
    }));
    Bench->Select("o_damage");
    AbilityProblemProperties problems(*Bench);
    const std::vector<PropertyNode> nodes = problems.Nodes();
    const auto fixable = std::ranges::find_if(nodes, [](const PropertyNode& node) { return !node.Descriptor.Verbs.empty(); });
    ASSERT_NE(fixable, nodes.end());
    ASSERT_EQ(problems.Act(fixable->Key, "fix", {}), PropertyEditResult::Applied);
    const JsonValue& when = *FindMember(*FindDataValue(Data(), FindAbilityElement(Data(), "o_damage")->Path), "when");
    ASSERT_NE(FindMember(when, "all"), nullptr);
    EXPECT_EQ(FindMember(when, "all")->AsArray().size(), 2u) << "the held guard and the recipient's";
}

TEST_F(AbilityWorkbenchTest, TheOverviewReadsTheBindingWithItsDefaults)
{
    AbilityOverviewProperties overview(*Bench);
    const std::vector<PropertyNode> nodes = overview.Nodes();
    EXPECT_EQ(Find(nodes, "overview.input")->Value,
              "Input.PrimaryFire \xC2\xB7 press_release \xC2\xB7 priority 100 \xC2\xB7 exclusive \xC2\xB7 consume "
              "when blocked \xC2\xB7 cancel on interrupt");
    EXPECT_EQ(Find(nodes, "overview.activation")->Value, "not State.Stunned, Weapon.Reloading, Cooldown.Charge");
    EXPECT_EQ(Find(nodes, "overview.problems")->Value, "none");
}

TEST_F(AbilityWorkbenchTest, TheAbilityStaysOpenWhileAnEffectIsShown)
{
    std::string error;
    const JsonValue starter(JsonValue::Object{ { "duration", JsonValue("instant") },
                                               { "modifiers", JsonValue(JsonValue::Array{}) } });
    ASSERT_NE(Bench->Documents.Create(kEffectDefinitionType, "effects/new_hurt.sdata", error, &starter), nullptr) << error;
    ASSERT_NE(Bench->Ability(), nullptr);
    EXPECT_EQ(Bench->Ability()->VirtualPath(), kCharge);
    EXPECT_FALSE(Bench->ShowingAbility());
    ASSERT_TRUE(Bench->Open(kCharge, error));
    EXPECT_TRUE(Bench->ShowingAbility());
}
