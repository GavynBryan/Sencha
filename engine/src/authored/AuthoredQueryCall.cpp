#include <authored/AuthoredQueryCall.h>

#include <authored/AuthoredArgumentMatch.h>
#include <authored/AuthoredLiteral.h>
#include <authored/AuthoredQueryRegistry.h>

#include <algorithm>
#include <cassert>
#include <format>

bool CompileAuthoredQueryCall(const AuthoredQueryRegistry& registry,
                              std::string_view query,
                              const JsonValue& constants,
                              std::span<const std::string_view> entityArguments,
                              std::span<std::size_t> entitySlots,
                              std::string_view subject,
                              AuthoredQueryCall& out,
                              std::vector<std::string>& errors)
{
    assert(entitySlots.size() == entityArguments.size());
    out.Query = registry.Resolve(query);
    if (!out.Query.IsValid())
    {
        errors.push_back(std::format("{}: '{}' is not a query this World declares", subject, query));
        return false;
    }
    const AuthoredQueryDefinition& definition = *registry.Get(out.Query.Slot);

    std::vector<std::string_view> names;
    for (const auto& [key, unused] : constants.AsObject())
        names.push_back(key);
    std::vector<std::string_view> undeclared;
    const std::vector<AuthoredArgumentSlot> slots = MatchAuthoredArguments(definition.Arguments, names, undeclared);
    std::vector<bool> found(entityArguments.size(), false);
    bool ok = true;
    out.Arguments.Resize(slots.size());
    for (std::size_t slot = 0; slot < slots.size(); ++slot)
    {
        const DataFieldSchema& field = *slots[slot].Field;
        const auto entity = std::ranges::find(entityArguments, field.Key);
        if (entity != entityArguments.end())
        {
            const std::size_t index = static_cast<std::size_t>(entity - entityArguments.begin());
            found[index] = field.Kind == DataFieldKind::Entity;
            entitySlots[index] = slot;
            continue;
        }
        AuthoredValue value;
        ok = (slots[slot].Given ? CompileAuthoredLiteral(constants.AsObject()[*slots[slot].Given].second, field,
                                                         subject, field.Key, value, errors)
                                : CompileAuthoredDefault(field, subject, field.Key, value, errors))
            && ok;
        out.Arguments.Set(slot, std::move(value));
    }
    for (std::size_t index = 0; index < entityArguments.size(); ++index)
    {
        if (found[index])
            continue;
        errors.push_back(std::format("{}: '{}' declares no entity argument '{}'", subject, query,
                                     entityArguments[index]));
        ok = false;
    }
    for (const std::string_view name : undeclared)
    {
        errors.push_back(std::format("{}: '{}' declares no argument '{}'", subject, query, name));
        ok = false;
    }
    return ok;
}
