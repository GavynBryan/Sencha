// The Workbench over the project's documents: an edit is one step of the
// journal, a selection follows its element through reordering and undo, a
// problem is revealed as its element selected with its field brought forward,
// and the inspector shows only the selected element.

#include "authoring/AbilityDocumentEdits.h"
#include "authoring/AbilityWorkbench.h"
#include "data/DataDocument.h"
#include "data/DataProperties.h"
#include "documents/DocumentSourceSet.h"
#include "presenters/PropertySheet.h"

#include <assets/runtime/RuntimeAssets.h>
#include <core/assets/AssetRegistry.h>
#include <world/serialization/ComponentSerializerRegistry.h>

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
};
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
