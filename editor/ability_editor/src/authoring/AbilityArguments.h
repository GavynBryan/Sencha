#pragma once

#include "properties/PropertySource.h"

#include <core/json/JsonValue.h>
#include <core/metadata/DataSchema.h>

#include <functional>
#include <string>
#include <string_view>
#include <vector>

class AbilityWorkbench;
class VocabularyCatalog;

// The arguments the verb, lease or query `op` runs takes, as its catalog
// declares them; null for an op that runs none of them, or one nothing declares.
[[nodiscard]] const DataFieldSchema* AbilityOpContract(const JsonValue& op, const VocabularyCatalog& names);

// Where an argument's value comes from.
enum class AbilityArgumentSource : std::uint8_t
{
    Unset,
    Constant,
    Port,
    Local,
};

// The sources an argument of `field` may take: a constant where one can be
// written, a port or a local where a value is read at runtime.
[[nodiscard]] std::vector<AbilityArgumentSource> AbilityArgumentSources(const DataFieldSchema& field);
[[nodiscard]] AbilityArgumentSource AbilityArgumentSourceOf(const JsonValue* argument);

// The selected op's arguments, laid out from its contract: each a source and a
// value, set as one step of the ability's journal.
class AbilityArgumentProperties final : public CommitOnlyPropertySource
{
public:
    // `createEffect` makes a new effect for the argument named, when one asks for one.
    AbilityArgumentProperties(AbilityWorkbench& workbench, std::function<void(std::string argument)> createEffect);

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;
    PropertyEditResult CommitEdit(std::string_view key, std::string_view value) override;
    [[nodiscard]] std::vector<std::string> Candidates(std::string_view key) const override;
    PropertyEditResult Act(std::string_view key, std::string_view verb, std::string_view choice) override;

private:
    AbilityWorkbench& Workbench;
    std::function<void(std::string)> CreateEffect;
};
