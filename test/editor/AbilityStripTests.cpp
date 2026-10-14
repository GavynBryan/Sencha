// The phase strip's rows, where a dragged element lands, where a new op goes,
// and how a guard reads: all from the document alone.

#include "authoring/AbilityConditionText.h"
#include "authoring/AbilityElements.h"
#include "authoring/AbilityPalette.h"
#include "authoring/AbilityStrip.h"
#include "data/JsonObjectEdit.h"
#include "vocabulary/VocabularyCatalog.h"

#include <core/json/JsonParser.h>

#include <gtest/gtest.h>

#include <algorithm>

namespace
{
constexpr std::string_view kTwoPhases = R"({
    "locals": [ { "key": "l_hit", "name": "Hit", "type": "hit" } ],
    "phases": [
      { "key": "p_aim", "name": "Aim", "timing": { "domain": "actor", "progress_ticks": 30 },
        "enter": [ { "key": "o_a", "op": "cue", "cue": "cue.a" }, { "key": "o_b", "op": "cue", "cue": "cue.b" },
                   { "key": "o_c", "op": "cue", "cue": "cue.c" } ],
        "hold": [ { "key": "o_hold", "lease": "anim.hold" } ],
        "present": [ { "key": "s_glow", "cue": "cue.charging" } ],
        "then": [ { "key": "t_go", "on": "release", "to": "p_fire" } ] },
      { "key": "p_fire", "name": "Fire",
        "enter": [ { "key": "o_trace", "op": "query", "query": "physics.trace", "into": "l_hit" },
                   { "key": "o_hurt", "op": "invoke", "verb": "effect.apply",
                     "when": { "key": "w_hit", "hit": "l_hit" } } ],
        "then": [ { "key": "t_end", "to": "end" } ] } ] })";

JsonValue TwoPhases()
{
    return *JsonParse(kTwoPhases);
}

std::vector<std::string> KeysIn(const JsonValue& data, std::string_view phase, std::string_view list)
{
    std::vector<std::string> keys;
    for (const AbilityElement& element : AbilityElementsOf(data))
        if (element.Kind == AbilityElementKind::Op && element.Phase == phase && element.List == list)
            keys.push_back(element.Key);
    return keys;
}

const UiRow& RowOf(const std::vector<UiRow>& rows, std::string_view key)
{
    return *std::ranges::find(rows, key, &UiRow::Key);
}
}

TEST(AbilityStrip, EachPhaseIsACardHoldingItsSlotsOpsAndTransitions)
{
    const JsonValue data = TwoPhases();
    const AbilityStripRows rows = AbilityStripOf(data, {}, "o_trace");
    ASSERT_EQ(rows.Cards.size(), 2u);
    EXPECT_EQ(rows.Cards[0].Label, "Aim");
    EXPECT_EQ(rows.Cards[0].Detail, "actor time \xC2\xB7 full in 30");
    EXPECT_EQ(rows.Cards[1].Detail, "actor time \xC2\xB7 1 in");
    EXPECT_EQ(rows.Slots.size(), 10u) << "four op lists a phase and what it presents";
    EXPECT_EQ(RowOf(rows.Ops, "o_hold").Value, AbilityStripSlotKey("p_aim", "hold"));
    EXPECT_EQ(RowOf(rows.Ops, "o_hold").Detail, "LEASE");
    EXPECT_EQ(RowOf(rows.Ops, "s_glow").Value, AbilityStripSlotKey("p_aim", "present"));
    EXPECT_EQ(RowOf(rows.Slots, AbilityStripSlotKey("p_aim", "present")).Detail, "fixed") << "listed, not added to";
    EXPECT_EQ(RowOf(rows.Ops, "o_hurt").Parts.front(), "Hit struck") << "its guard, as it reads";
    EXPECT_EQ(RowOf(rows.Links, "t_go").Value, "p_aim");
    EXPECT_TRUE(RowOf(rows.Ops, "o_trace").Selected);
    EXPECT_TRUE(rows.Cards[1].Expanded) << "the card holding the selection";
    EXPECT_FALSE(rows.Cards[0].Expanded);
}

TEST(AbilityStrip, AProblemLightsItsRowAndItsCard)
{
    const JsonValue data = TwoPhases();
    const AbilityDiagnostic unknown{ .Severity = AbilityDiagnosticSeverity::Error, .Code = "verb_unknown",
                                     .ElementKey = {}, .Path = "$.data.phases[1].enter[1].verb", .Message = "?" };
    const AbilityDiagnostic slow{ .Severity = AbilityDiagnosticSeverity::Warning, .Code = "unreachable",
                                  .ElementKey = "p_fire", .Path = "$.data.phases[1]", .Message = "?" };
    const AbilityStripRows rows = AbilityStripOf(data, std::vector{ unknown, slow }, {});
    EXPECT_EQ(RowOf(rows.Ops, "o_hurt").Parts[1], "error");
    EXPECT_EQ(rows.Cards[1].Parts[1], "error") << "the worst under it";
    EXPECT_EQ(rows.Cards[0].Parts[1], "");
}

TEST(AbilityStrip, ADraggedElementLandsWhereItIsDropped)
{
    JsonValue data = TwoPhases();
    ASSERT_TRUE(DropAbilityElement(data, "o_a", "o_c"));
    EXPECT_EQ(KeysIn(data, "p_aim", "enter"), (std::vector<std::string>{ "o_b", "o_a", "o_c" })) << "just before it";
    ASSERT_TRUE(DropAbilityElement(data, "o_c", "o_b"));
    EXPECT_EQ(KeysIn(data, "p_aim", "enter"), (std::vector<std::string>{ "o_c", "o_b", "o_a" }));
    ASSERT_TRUE(DropAbilityElement(data, "o_a", AbilityStripSlotKey("p_fire", "exit")));
    EXPECT_EQ(KeysIn(data, "p_fire", "exit"), (std::vector<std::string>{ "o_a" }));
    ASSERT_TRUE(DropAbilityElement(data, "o_b", "p_fire"));
    EXPECT_EQ(KeysIn(data, "p_fire", "enter").back(), "o_b") << "a card takes it at the end of its entry";
    ASSERT_TRUE(DropAbilityElement(data, "p_fire", "p_aim"));
    EXPECT_EQ(AbilityElementsOf(data).front().Kind, AbilityElementKind::Local);
    EXPECT_EQ(FindAbilityElement(data, "p_fire")->Index, 0u);
    EXPECT_FALSE(DropAbilityElement(data, "t_go", "o_trace")) << "a transition is not dropped among ops";
    EXPECT_FALSE(DropAbilityElement(data, "o_trace", "o_trace"));
}

TEST(AbilityPalette, EveryNameAnOpCanRunIsOfferedWithItsBadges)
{
    const VocabularyCatalog names;
    const std::vector<AbilityPaletteEntry> entries = AbilityPaletteOf(names);
    const auto find = [&](std::string_view key) { return std::ranges::find(entries, key, &AbilityPaletteEntry::Key); };
    ASSERT_NE(find("lease:time.scale"), entries.end());
    EXPECT_EQ(find("lease:time.scale")->Group, "Leases");
    EXPECT_EQ(find("lease:time.scale")->Badges, "gameplay \xC2\xB7 simulation");
    ASSERT_NE(find("verb:effect.apply"), entries.end());
    ASSERT_NE(find("query:physics.trace"), entries.end());
    ASSERT_NE(find("kit:commit"), entries.end());
    EXPECT_EQ(ReadMemberString(AbilityPaletteOp("lease:time.scale"), "lease"), "time.scale");
    EXPECT_EQ(ReadMemberString(AbilityPaletteOp("query:physics.trace"), "op"), "query");
    EXPECT_TRUE(AbilityPaletteOp("verb:").IsObject());
    EXPECT_TRUE(AbilityPaletteOp("nothing").IsNull());
}

TEST(AbilityPalette, ANewOpGoesWhereItWasAskedForAndALeaseOnlyWhereItIsHeld)
{
    const JsonValue data = TwoPhases();
    const auto at = [&](std::string_view slot, std::string_view selected, bool lease) {
        return AbilityOpPlacementFor(data, slot, selected, lease);
    };
    EXPECT_EQ(at(AbilityStripSlotKey("p_fire", "exit"), {}, false)->List, AbilityOpList::Exit);
    const std::optional<AbilityOpPlacement> after = at({}, "o_a", false);
    ASSERT_TRUE(after);
    EXPECT_EQ(after->Phase, "p_aim");
    EXPECT_EQ(after->Index, 1u) << "just after the selected op";
    EXPECT_EQ(at({}, "t_end", false)->Phase, "p_fire");
    EXPECT_EQ(at({}, "o_a", true)->List, AbilityOpList::Hold) << "a lease is held";
    EXPECT_EQ(at(AbilityStripSlotKey("p_aim", "hold"), {}, false)->List, AbilityOpList::Enter)
        << "and nothing else is";
    EXPECT_FALSE(at({}, "l_hit", false).has_value()) << "no phase to put it in";
}

TEST(AbilityConditionText, AGuardReadsAsWhatItChecks)
{
    const JsonValue data = TwoPhases();
    const auto text = [&](std::string_view json) { return AbilityConditionText(data, *JsonParse(json)); };
    EXPECT_EQ(text(R"({ "hit": "l_hit" })"), "Hit struck");
    EXPECT_EQ(text(R"({ "phase_ticks": { "at_least": 6 } })"), "phase ticks \xE2\x89\xA5 6");
    EXPECT_EQ(text(R"({ "entity": { "of": { "local": "l_hit", "field": "entity" }, "has": ["sencha.attribute_set"] } })"),
              "entity(Hit.entity) has sencha.attribute_set");
    EXPECT_EQ(text(R"({ "tags": { "all": ["A.B"], "none": ["C"] } })"), "has A.B; not C");
    EXPECT_EQ(text(R"({ "attribute": { "name": "Energy", "above": 0 } })"), "Energy > 0");
    EXPECT_EQ(text(R"({ "not": { "input": "held" } })"), "not (input held)");
}
