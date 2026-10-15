// An ability's elements are found and edited by key: a selection survives
// reordering, a deleted phase leaves no transition pointing nowhere, a copy keys
// everything inside it afresh, and every recipe is an ability that compiles.

#include "authoring/AbilityChecks.h"
#include "authoring/AbilityDocumentEdits.h"
#include "authoring/AbilityElements.h"
#include "authoring/AbilityRecipes.h"
#include "data/DataPath.h"
#include "data/JsonObjectEdit.h"
#include "vocabulary/VocabularyCatalog.h"

#include <abilities/AbilityDefinitionData.h>
#include <core/json/JsonParser.h>

#include <gtest/gtest.h>

#include <set>

namespace
{
constexpr std::string_view kCharge = R"({
    "input_default": { "action": "Input.PrimaryFire", "policy": "press_release" },
    "locals": [ { "key": "l_charge", "name": "Charge", "type": "float" } ],
    "phases": [
      { "key": "p_charge", "name": "Charge",
        "exit": [ { "key": "o_capture", "op": "set", "local": "l_charge", "from": { "port": "phase.progress" } } ],
        "then": [
          { "key": "t_fire", "on": "release", "when": { "key": "w_min", "phase_ticks": { "at_least": 6 } }, "to": "p_fire" },
          { "key": "t_early", "on": "release", "to": "p_recover" } ] },
      { "key": "p_fire", "name": "Fire",
        "enter": [ { "key": "o_mark", "op": "invoke", "verb": "effect.apply" } ],
        "then": [ { "key": "t_recover", "to": "p_recover" } ] },
      { "key": "p_recover", "name": "Recover", "then": [ { "key": "t_done", "on": "timeout", "ticks": 20, "to": "end" } ] } ] })";

JsonValue Charge()
{
    return *JsonParse(kCharge);
}

std::vector<std::string> PhaseKeys(const JsonValue& data)
{
    std::vector<std::string> keys;
    for (const JsonValue& phase : FindMember(data, "phases")->AsArray())
        keys.push_back(ReadMemberString(phase, "key"));
    return keys;
}

std::string TransitionTo(const JsonValue& data, std::string_view key)
{
    const std::optional<AbilityElement> element = FindAbilityElement(data, key);
    return element ? ReadMemberString(*FindDataValue(data, element->Path), "to") : std::string{};
}
}

TEST(AbilityElements, EveryKeyedElementIsFoundWithWhereItSits)
{
    const JsonValue data = Charge();
    const std::optional<AbilityElement> guard = FindAbilityElement(data, "w_min");
    ASSERT_TRUE(guard);
    EXPECT_EQ(guard->Kind, AbilityElementKind::Condition);
    EXPECT_EQ(guard->Path, "$.data.phases[0].then[0].when");
    EXPECT_EQ(guard->Phase, "p_charge");
    const std::optional<AbilityElement> mark = FindAbilityElement(data, "o_mark");
    ASSERT_TRUE(mark);
    EXPECT_EQ(mark->Kind, AbilityElementKind::Op);
    EXPECT_EQ(mark->List, "enter");
    EXPECT_EQ(AbilityElementLabel(data, *mark), "effect.apply");
    EXPECT_EQ(AbilityElementLabel(data, *FindAbilityElement(data, "o_capture")), "set Charge");
    EXPECT_EQ(AbilityElementLabel(data, *FindAbilityElement(data, "t_early")), "on release \xE2\x86\x92 Recover");
}

TEST(AbilityElements, AProblemsPathNamesTheInnermostElement)
{
    const JsonValue data = Charge();
    EXPECT_EQ(AbilityElementAt(data, "$.data.phases[0].then[0].when.phase_ticks")->Key, "w_min");
    EXPECT_EQ(AbilityElementAt(data, "$.data.phases[1].enter[0].verb")->Key, "o_mark");
    EXPECT_EQ(AbilityElementAt(data, "$.data.phases[1]")->Key, "p_fire");
    EXPECT_FALSE(AbilityElementAt(data, "$.data.input_default.action").has_value());
}

TEST(AbilityDocumentEdits, APhaseIsAddedAfterTheOneAskedAndMovedByKey)
{
    JsonValue data = Charge();
    const std::string added = AddAbilityPhase(data, "Aim", "p_charge");
    EXPECT_EQ(PhaseKeys(data), (std::vector<std::string>{ "p_charge", added, "p_fire", "p_recover" }));
    ASSERT_TRUE(MoveAbilityPhase(data, "p_recover", 0));
    EXPECT_EQ(PhaseKeys(data), (std::vector<std::string>{ "p_recover", "p_charge", added, "p_fire" }));
    EXPECT_EQ(FindAbilityElement(data, "w_min")->Phase, "p_charge") << "found by key wherever it now is";
}

TEST(AbilityDocumentEdits, DeletingAPhaseEndsTheTransitionsThatWentToIt)
{
    JsonValue data = Charge();
    EXPECT_EQ(AbilityTransitionsInto(data, "p_recover"), 2u);
    ASSERT_TRUE(RemoveAbilityPhase(data, "p_recover"));
    EXPECT_EQ(PhaseKeys(data), (std::vector<std::string>{ "p_charge", "p_fire" }));
    EXPECT_EQ(TransitionTo(data, "t_early"), "end");
    EXPECT_EQ(TransitionTo(data, "t_recover"), "end");
    EXPECT_EQ(TransitionTo(data, "t_fire"), "p_fire") << "the others are untouched";
}

TEST(AbilityDocumentEdits, AnOpMovesBetweenPhasesAndLists)
{
    JsonValue data = Charge();
    const std::string pulse = InsertAbilityOp(data, "p_charge", AbilityOpList::While, 0,
                                              JsonValue(JsonValue::Object{ { "op", JsonValue("cue") } }));
    ASSERT_FALSE(pulse.empty());
    const std::optional<AbilityElement> inserted = FindAbilityElement(data, pulse);
    ASSERT_TRUE(inserted);
    EXPECT_EQ(inserted->Path, "$.data.phases[0].while.ops[0]");
    ASSERT_TRUE(MoveAbilityOp(data, "o_mark", "p_charge", AbilityOpList::Enter, 0));
    EXPECT_EQ(FindAbilityElement(data, "o_mark")->Path, "$.data.phases[0].enter[0]");
    EXPECT_TRUE(FindMember(FindMember(data, "phases")->AsArray()[1], "enter")->AsArray().empty());
    EXPECT_FALSE(MoveAbilityOp(data, "t_done", "p_charge", AbilityOpList::Enter, 0)) << "only an op moves as one";
}

TEST(AbilityDocumentEdits, ACopyKeysEverythingInsideItAfresh)
{
    JsonValue data = Charge();
    const std::string copy = DuplicateAbilityElement(data, "t_fire");
    ASSERT_FALSE(copy.empty());
    std::set<std::string> keys;
    for (const AbilityElement& element : AbilityElementsOf(data))
        EXPECT_TRUE(keys.insert(element.Key).second) << "two elements share '" << element.Key << "'";
    const std::optional<AbilityElement> duplicate = FindAbilityElement(data, copy);
    ASSERT_TRUE(duplicate);
    EXPECT_EQ(duplicate->Index, 1u) << "just after the original";
    EXPECT_EQ(TransitionTo(data, copy), "p_fire");
}

TEST(AbilityDocumentEdits, AGuardIsSetAndTakenAway)
{
    JsonValue data = Charge();
    const std::string guard =
        SetAbilityGuard(data, "o_mark", JsonValue(JsonValue::Object{ { "hit", JsonValue("l_charge") } }));
    ASSERT_FALSE(guard.empty());
    EXPECT_EQ(FindAbilityElement(data, guard)->Path, "$.data.phases[1].enter[0].when");
    (void)SetAbilityGuard(data, "o_mark", JsonValue());
    EXPECT_FALSE(FindAbilityElement(data, guard).has_value());
    EXPECT_TRUE(SetAbilityGuard(data, "p_fire", JsonValue(JsonValue::Object{})).empty()) << "a phase takes no guard";
}

TEST(AbilityRecipes, EveryRecipeIsAnAbilityTheEngineResolves)
{
    const VocabularyCatalog names;
    for (const AbilityRecipe& recipe : AbilityRecipes())
    {
        const JsonValue data = recipe.Make();
        for (const AbilityDiagnostic& diagnostic : CheckAbilityData(data, "asset://abilities/new.sdata", names.Names()))
            EXPECT_NE(diagnostic.Severity, AbilityDiagnosticSeverity::Error)
                << recipe.Name << ": " << diagnostic.Code << " " << diagnostic.Path << " " << diagnostic.Message;
        EXPECT_NE(recipe.Make().Find("phases")->AsArray().front().Find("key")->AsString(),
                  data.Find("phases")->AsArray().front().Find("key")->AsString())
            << recipe.Name << " mints its keys each time it is made";
    }
}

TEST(AbilityChecks, AProblemNamesItsElementAndKeepsItsPath)
{
    JsonValue data = Charge();
    (void)SetAbilityElementMember(data, "o_mark", "verb", JsonValue("effect.aply"));
    const VocabularyCatalog names;
    const std::vector<AbilityDiagnostic> diagnostics = CheckAbilityData(data, "asset://abilities/charge.sdata",
                                                                        names.Names());
    const auto unknown = std::ranges::find(diagnostics, std::string("verb_unknown"), &AbilityDiagnostic::Code);
    ASSERT_NE(unknown, diagnostics.end());
    const std::vector<Problem> problems = AbilityProblems({ *unknown }, data, DocumentRef{});
    ASSERT_EQ(problems.size(), 1u);
    EXPECT_TRUE(problems.front().Message.starts_with("Fire \xE2\x80\xBA effect.aply: ")) << problems.front().Message;
    EXPECT_EQ(AbilityElementAt(data, problems.front().Location)->Key, "o_mark");
}
