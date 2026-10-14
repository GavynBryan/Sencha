#include "authoring/AbilityRecipes.h"

#include "authoring/AbilityDocumentEdits.h"
#include "data/JsonObjectEdit.h"

#include <array>
#include <utility>

namespace
{
JsonValue Started(std::string_view policy)
{
    JsonValue data(JsonValue::Object{});
    JsonValue input(JsonValue::Object{});
    SetMember(input, "action", JsonValue("Input.PrimaryFire"));
    SetMember(input, "policy", JsonValue(std::string(policy)));
    SetMember(data, "input_default", std::move(input));
    return data;
}

JsonValue Object(std::initializer_list<std::pair<std::string_view, JsonValue>> members)
{
    JsonValue object(JsonValue::Object{});
    for (const auto& [name, value] : members)
        SetMember(object, name, value);
    return object;
}

JsonValue Blank()
{
    JsonValue data(JsonValue::Object{});
    const std::string run = AddAbilityPhase(data, "Run");
    (void)AddAbilityTransition(data, run, {}, "end");
    return data;
}

JsonValue Instant()
{
    JsonValue data = Started("press");
    const std::string fire = AddAbilityPhase(data, "Fire");
    (void)AddAbilityTransition(data, fire, {}, "end");
    return data;
}

JsonValue Periodic()
{
    JsonValue data = Started("press_release");
    const std::string repeat = AddAbilityPhase(data, "Repeat");
    (void)SetAbilityElementMember(data, repeat, "while",
                                  Object({ { "every", JsonValue(10) }, { "first", JsonValue("on_entry") },
                                           { "ops", JsonValue(JsonValue::Array{}) } }));
    (void)AddAbilityTransition(data, repeat, "release", "end");
    return data;
}

JsonValue ChargeRelease()
{
    JsonValue data = Started("press_release");
    const std::string charge = AddAbilityLocal(data, "Charge", "float");
    const std::string charging = AddAbilityPhase(data, "Charge");
    const std::string fire = AddAbilityPhase(data, "Fire");
    const std::string recover = AddAbilityPhase(data, "Recover");
    (void)SetAbilityElementMember(data, charging, "timing",
                                  Object({ { "domain", JsonValue("actor") }, { "progress_ticks", JsonValue(60) } }));
    (void)InsertAbilityOp(data, charging, AbilityOpList::Exit, 0,
                          Object({ { "op", JsonValue("set") }, { "local", JsonValue(charge) },
                                   { "from", Object({ { "port", JsonValue("phase.progress") } }) } }));
    const std::string released = AddAbilityTransition(data, charging, "release", fire);
    (void)SetAbilityGuard(data, released, Object({ { "phase_ticks", Object({ { "at_least", JsonValue(6) } }) } }));
    (void)AddAbilityTransition(data, charging, "release", recover);
    (void)AddAbilityTransition(data, fire, {}, recover);
    const std::string done = AddAbilityTransition(data, recover, "timeout", "end");
    (void)SetAbilityElementMember(data, done, "ticks", JsonValue(20));
    return data;
}

JsonValue SustainedToggle()
{
    JsonValue data = Started("toggle");
    const std::string active = AddAbilityPhase(data, "Active");
    (void)InsertAbilityOp(data, active, AbilityOpList::Hold, 0,
                          Object({ { "lease", JsonValue("time.scale") },
                                   { "arguments", Object({ { "target", Object({ { "port", JsonValue("actor") } }) },
                                                           { "scale", Object({ { "const", JsonValue(0.5) } }) } }) } }));
    (void)AddAbilityTransition(data, active, "release", "end");
    return data;
}

constexpr std::array<AbilityRecipe, 5> kRecipes{ {
    { "Blank", "One phase that ends at once.", &Blank },
    { "Instant", "Runs once on a press.", &Instant },
    { "Held / Periodic", "Pulses while held, every ten ticks.", &Periodic },
    { "Charge / Release", "Charges while held; a long enough release fires, a short one recovers.", &ChargeRelease },
    { "Sustained Toggle", "On until pressed again, holding a lease while on.", &SustainedToggle },
} };
} // namespace

std::span<const AbilityRecipe> AbilityRecipes()
{
    return kRecipes;
}

const AbilityRecipe* FindAbilityRecipe(std::string_view name)
{
    for (const AbilityRecipe& recipe : kRecipes)
        if (recipe.Name == name)
            return &recipe;
    return nullptr;
}
