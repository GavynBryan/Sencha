// The ability.definition compiler's World-free half: what it accepts, and every
// structural problem it names with the element's key and path.

#include <abilities/AbilityCapacity.h>
#include <abilities/AbilityDefinitionData.h>
#include <core/json/JsonParser.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <string>
#include <vector>

namespace
{
struct Compiled
{
    bool Ok = false;
    AbilityDeclaration Declaration;
    std::vector<AssetRef> Dependencies;
    std::vector<AbilityDiagnostic> Diagnostics;

    [[nodiscard]] const AbilityDiagnostic* Find(std::string_view code) const
    {
        const auto found = std::ranges::find(Diagnostics, code, &AbilityDiagnostic::Code);
        return found != Diagnostics.end() ? &*found : nullptr;
    }

    [[nodiscard]] std::string Describe() const
    {
        std::string text;
        for (const AbilityDiagnostic& diagnostic : Diagnostics)
            text += diagnostic.Code + " " + diagnostic.ElementKey + " " + diagnostic.Path + ": " + diagnostic.Message + "\n";
        return text;
    }
};

Compiled Compile(std::string_view json)
{
    Compiled result;
    const std::optional<JsonValue> data = JsonParse(json);
    EXPECT_TRUE(data.has_value()) << json;
    if (data)
        result.Ok = CompileAbilityDeclaration(*data, result.Declaration, result.Dependencies, result.Diagnostics);
    return result;
}

std::filesystem::path FixtureAsset(std::string_view relative)
{
    return std::filesystem::path(SENCHA_REPO_ROOT) / "test/fixtures/abilities/assets" / relative;
}

struct Rejection
{
    const char* Name;
    const char* Code;
    const char* Key;
    const char* Json;
};

// Each is the smallest ability with exactly the problem named.
constexpr Rejection kRejections[] = {
    { "duplicate phase keys", "key_duplicate", "p_a", R"({ "phases": [
        { "key": "p_a", "then": [ { "key": "t_1", "to": "end" } ] },
        { "key": "p_a", "then": [ { "key": "t_2", "to": "end" } ] } ] })" },
    { "transition to nowhere", "phase_unknown", "t_1", R"({ "phases": [
        { "key": "p_a", "then": [ { "key": "t_1", "to": "p_missing" } ] } ] })" },
    { "phase without exit", "no_exit", "p_a", R"({ "phases": [ { "key": "p_a" } ] })" },
    { "immediate loop", "zero_time_cycle", "t_back", R"({ "phases": [
        { "key": "p_a", "then": [ { "key": "t_go", "to": "p_b" } ] },
        { "key": "p_b", "then": [ { "key": "t_back", "to": "p_a" } ] } ] })" },
    { "loop closed by a release", "zero_time_cycle", "t_back", R"({ "phases": [
        { "key": "p_a", "then": [ { "key": "t_go", "on": "release", "to": "p_b" } ] },
        { "key": "p_b", "then": [ { "key": "t_back", "to": "p_a" }, { "key": "t_out", "on": "timeout", "ticks": 1, "to": "end" } ] } ] })" },
    { "impact at a hit nothing guards", "unguarded_maybe_empty", "o_cue", R"({
        "locals": [ { "key": "l_hit", "type": "hit" } ],
        "phases": [ { "key": "p_a",
            "enter": [ { "key": "o_cue", "op": "cue", "cue": "cue.impact", "at": { "local": "l_hit" } } ],
            "then": [ { "key": "t_1", "to": "end" } ] } ] })" },
    { "damage to an unguarded entity", "unguarded_recipient", "o_hit", R"({
        "locals": [ { "key": "l_hit", "type": "hit" } ],
        "phases": [ { "key": "p_a",
            "enter": [ { "key": "o_hit", "op": "invoke", "verb": "effect.apply",
                         "arguments": { "target": { "local": "l_hit", "field": "entity" } } } ],
            "then": [ { "key": "t_1", "to": "end" } ] } ] })" },
    { "set into a hit", "local_type", "o_set", R"({
        "locals": [ { "key": "l_hit", "type": "hit" } ],
        "phases": [ { "key": "p_a",
            "enter": [ { "key": "o_set", "op": "set", "local": "l_hit", "from": 1 } ],
            "then": [ { "key": "t_1", "to": "end" } ] } ] })" },
    { "unknown local", "local_unknown", "o_set", R"({ "phases": [ { "key": "p_a",
            "enter": [ { "key": "o_set", "op": "set", "local": "l_nope", "from": 1 } ],
            "then": [ { "key": "t_1", "to": "end" } ] } ] })" },
    { "once group in a pulse", "commit_repeat", "o_pay", R"({
        "costs": [ { "key": "g_shot", "effects": [ { "data": "asset://effects/ammo_1.sdata" } ] } ],
        "phases": [ { "key": "p_a",
            "while": { "every": 5, "ops": [ { "key": "o_pay", "op": "commit", "groups": ["g_shot"] } ] },
            "then": [ { "key": "t_1", "on": "release", "to": "end" } ] } ] })" },
    { "unknown group", "group_unknown", "o_pay", R"({ "phases": [ { "key": "p_a",
            "enter": [ { "key": "o_pay", "op": "commit", "groups": ["g_none"] } ],
            "then": [ { "key": "t_1", "to": "end" } ] } ] })" },
    { "start group in a commit", "cost_shape", "o_pay", R"({
        "costs": [ { "key": "g_start", "when": "start", "effects": [ { "data": "asset://effects/ammo_1.sdata" } ] } ],
        "phases": [ { "key": "p_a", "enter": [ { "key": "o_pay", "op": "commit", "groups": ["g_start"] } ],
            "then": [ { "key": "t_1", "to": "end" } ] } ] })" },
    { "too many local slots", "capacity", "", R"({
        "locals": [ { "key": "l_1", "type": "hit" }, { "key": "l_2", "type": "hit" }, { "key": "l_3", "type": "hit" },
                    { "key": "l_4", "type": "hit" }, { "key": "l_5", "type": "float" } ],
        "phases": [ { "key": "p_a", "then": [ { "key": "t_1", "to": "end" } ] } ] })" },
    { "misspelled list", "schema", "p_a", R"({ "phases": [
        { "key": "p_a", "enter_ops": [], "then": [ { "key": "t_1", "to": "end" } ] } ] })" },
    { "lease outside hold", "schema", "o_hold", R"({ "phases": [ { "key": "p_a",
            "enter": [ { "key": "o_hold", "lease": "anim.hold" } ],
            "then": [ { "key": "t_1", "to": "end" } ] } ] })" },
    { "target port without targets", "port_unknown", "o_hit", R"({ "phases": [ { "key": "p_a",
            "enter": [ { "key": "o_hit", "op": "invoke", "verb": "effect.apply",
                         "arguments": { "target": { "port": "target" } } } ],
            "then": [ { "key": "t_1", "to": "end" } ] } ] })" },
    { "phase ticks outside a phase", "schema", "act_late", R"({
        "activation": [ { "key": "act_late", "phase_ticks": { "at_least": 3 } } ],
        "phases": [ { "key": "p_a", "then": [ { "key": "t_1", "to": "end" } ] } ] })" },
    { "zero-tick timeout", "schema", "t_1", R"({ "phases": [
        { "key": "p_a", "then": [ { "key": "t_1", "on": "timeout", "to": "end" } ] } ] })" },
    { "conditions nested too deep", "capacity", "c_3", R"({
        "activation": [ { "key": "c_1", "all": [ { "key": "c_2", "any": [ { "key": "c_3", "not":
            { "key": "c_4", "tags": { "all": ["A"] } } } ] } ] } ],
        "phases": [ { "key": "p_a", "then": [ { "key": "t_1", "to": "end" } ] } ] })" },
};
}

TEST(AbilityDefinitionData, TheNormativeChargeCompilesClean)
{
    const std::optional<JsonValue> file = JsonParseFile(FixtureAsset("abilities/charge_release.sdata"));
    ASSERT_TRUE(file.has_value());
    const JsonValue* data = file->Find("data");
    ASSERT_NE(data, nullptr);

    Compiled compiled;
    compiled.Ok = CompileAbilityDeclaration(*data, compiled.Declaration, compiled.Dependencies, compiled.Diagnostics);
    ASSERT_TRUE(compiled.Ok) << compiled.Describe();
    EXPECT_TRUE(compiled.Diagnostics.empty()) << compiled.Describe();

    const AbilityDeclaration& charge = compiled.Declaration;
    EXPECT_EQ(charge.DisplayName, "Charge shot");
    ASSERT_TRUE(charge.InputDefault.has_value());
    EXPECT_EQ(charge.InputDefault->Action, "Input.PrimaryFire");
    EXPECT_EQ(charge.InputDefault->Policy, AbilityInputPolicy::PressRelease);
    EXPECT_EQ(charge.InputDefault->Priority, 100);
    ASSERT_EQ(charge.Phases.size(), 3u);
    EXPECT_EQ(charge.Phases[0].ProgressTicks, 60u);
    ASSERT_EQ(charge.Phases[0].Hold.size(), 1u);
    EXPECT_EQ(charge.Phases[0].Hold[0].Kind, AbilityOpKind::Lease);
    EXPECT_EQ(charge.Phases[0].Then[0].Trigger, AbilityTransitionTrigger::Release);
    ASSERT_TRUE(charge.Phases[0].Then[0].When.has_value());
    EXPECT_EQ(charge.Phases[0].Then[0].When->Kind, AbilityConditionKind::PhaseTicks);
    EXPECT_TRUE(charge.Phases[2].Then[0].To.empty()) << "'end'";
    ASSERT_EQ(charge.Phases[1].Enter.size(), 5u);
    EXPECT_EQ(charge.Phases[1].Enter[2].When->Kind, AbilityConditionKind::Entity);
    ASSERT_TRUE(charge.Cooldown.has_value());
    EXPECT_EQ(charge.Cooldown->Start, AbilityCooldownStart::OnCommit);
    EXPECT_EQ(charge.Prediction, AbilityPredictionLevel::Simulation);

    std::vector<std::string> paths;
    for (const AssetRef& dependency : compiled.Dependencies)
        paths.push_back(dependency.Path);
    for (const char* effect : { "asset://effects/ammo_1.sdata", "asset://effects/charge_cooldown.sdata",
                                "asset://effects/charge_damage.sdata" })
        EXPECT_NE(std::ranges::find(paths, effect), paths.end()) << effect;
}

TEST(AbilityDefinitionData, EachStructuralProblemIsNamedWithItsElement)
{
    for (const Rejection& rejection : kRejections)
    {
        const Compiled compiled = Compile(rejection.Json);
        EXPECT_FALSE(compiled.Ok) << rejection.Name;
        const AbilityDiagnostic* found = compiled.Find(rejection.Code);
        ASSERT_NE(found, nullptr) << rejection.Name << "\n" << compiled.Describe();
        EXPECT_EQ(found->Severity, AbilityDiagnosticSeverity::Error) << rejection.Name;
        EXPECT_EQ(found->ElementKey, rejection.Key) << rejection.Name << "\n" << compiled.Describe();
        EXPECT_FALSE(found->Path.empty()) << rejection.Name;
    }
}

TEST(AbilityDefinitionData, WarningsDoNotRefuse)
{
    const Compiled unreachable = Compile(R"({ "phases": [
        { "key": "p_a", "then": [ { "key": "t_1", "to": "end" } ] },
        { "key": "p_lost", "name": "Lost", "then": [ { "key": "t_2", "to": "end" } ] } ] })");
    EXPECT_TRUE(unreachable.Ok) << unreachable.Describe();
    ASSERT_NE(unreachable.Find("unreachable"), nullptr);
    EXPECT_EQ(unreachable.Find("unreachable")->ElementKey, "p_lost");

    const Compiled twice = Compile(R"({
        "costs": [ { "key": "g_shot", "effects": [ { "data": "asset://effects/ammo_1.sdata" } ] } ],
        "phases": [
          { "key": "p_a", "enter": [ { "key": "o_1", "op": "commit", "groups": ["g_shot"] } ],
            "then": [ { "key": "t_1", "to": "p_b" } ] },
          { "key": "p_b", "enter": [ { "key": "o_2", "op": "commit", "groups": ["g_shot"] } ],
            "then": [ { "key": "t_2", "to": "end" } ] } ] })");
    EXPECT_TRUE(twice.Ok) << twice.Describe();
    ASSERT_NE(twice.Find("commit_twice"), nullptr);
}

TEST(AbilityDefinitionData, AGuardInsideATopLevelAllCounts)
{
    const Compiled guarded = Compile(R"({
        "locals": [ { "key": "l_hit", "type": "hit" } ],
        "phases": [ { "key": "p_a",
            "enter": [ { "key": "o_hit", "op": "invoke", "verb": "effect.apply",
                         "when": { "key": "w_all", "all": [
                             { "key": "w_struck", "hit": "l_hit" },
                             { "key": "w_live", "entity": { "of": { "local": "l_hit", "field": "entity" } } } ] },
                         "arguments": { "target": { "local": "l_hit", "field": "entity" },
                                        "at": { "local": "l_hit", "field": "point" } } } ],
            "then": [ { "key": "t_1", "to": "end" } ] } ] })");
    EXPECT_TRUE(guarded.Ok) << guarded.Describe();
}

// A sustained presentation names its lease by the present's place in eight
// bits, so a phase with more presents than that would have two share one.
TEST(AbilityDefinitionData, APhasePresentsAtMostWhatALeaseKeyCanName)
{
    const auto phaseWith = [](std::size_t presents) {
        std::string list;
        for (std::size_t i = 0; i < presents; ++i)
            list += std::format(R"({}{{ "key": "pr_{}", "cue": "cue.glow" }})", i == 0 ? "" : ", ", i);
        return Compile(std::format(R"({{ "input_default": {{ "action": "Input.PrimaryFire", "policy": "press_release" }},
            "phases": [ {{ "key": "p_on", "present": [ {} ],
                           "then": [ {{ "key": "t_off", "on": "release", "to": "end" }} ] }} ] }})",
                                   list));
    };
    EXPECT_TRUE(phaseWith(kAbilityMaxPhasePresents).Ok) << phaseWith(kAbilityMaxPhasePresents).Describe();
    const Compiled over = phaseWith(kAbilityMaxPhasePresents + 1);
    EXPECT_FALSE(over.Ok);
    EXPECT_NE(over.Describe().find("present"), std::string::npos) << over.Describe();
}
