#pragma once

#include <authored/AuthoredCatalog.h>
#include <authored/AuthoredSchema.h>
#include <authored/AuthoredValue.h>
#include <core/identity/StrongId.h>
#include <core/metadata/DataSchema.h>
#include <ecs/EntityId.h>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

class World;

struct AbilityCostRow
{
    EntityId Actor;
    EntityId Source;
    std::span<const AuthoredValue> Arguments;
};

struct AbilityCostProviderCheck
{
    bool Affordable = true;
    std::uint32_t Row = 0;
};

using AbilityCostCheckFn = AbilityCostProviderCheck (*)(const World& world, std::span<const AbilityCostRow> rows);
// Runs only after every check of the commit passed, on the owner thread, with
// nothing between; it cannot fail, and changes no archetype.
using AbilityCostApplyFn = void (*)(World& world, std::span<const AbilityCostRow> rows);
using AbilityCostTranslateFn = bool (*)(const World& world, const AbilityCostRow& row,
                                        std::vector<AuthoredValue>& arguments);

struct AbilityCostProviderDefinition
{
    std::string Name;
    std::string DisplayName;
    std::string Description;
    DataFieldSchema Arguments = AuthoredRecordRoot();
    AbilityCostCheckFn Check = nullptr;
    AbilityCostApplyFn Apply = nullptr;
    std::string SpendsThrough;
    AbilityCostTranslateFn Translate = nullptr;
};

using AbilityCostProviderId = StrongId<struct AbilityCostProviderIdTag, std::uint32_t>;
using AbilityCostProviderRevision = StrongId<struct AbilityCostProviderRevisionTag, std::uint32_t>;
using AbilityCostProviderCatalogId = StrongId<struct AbilityCostProviderCatalogTag, std::uint64_t>;

struct AbilityCostProviderCatalogTraits
{
    using Definition = AbilityCostProviderDefinition;
    using Id = AbilityCostProviderId;
    using Revision = AbilityCostProviderRevision;
    using CatalogId = AbilityCostProviderCatalogId;
    static constexpr std::string_view Noun = "cost provider";

    static void Validate(const AbilityCostProviderDefinition& definition, std::vector<std::string>& errors);

    static bool ContractsMatch(const AbilityCostProviderDefinition& left, const AbilityCostProviderDefinition& right)
    {
        return AuthoredContractsMatch(left.Arguments, right.Arguments) && left.SpendsThrough == right.SpendsThrough;
    }
};

using AbilityCostProviderCatalog = AuthoredCatalog<AbilityCostProviderCatalogTraits>;
using AbilityCostProviderRegistrationScope = AuthoredRegistrationScope<AbilityCostProviderCatalogTraits>;
using AbilityCostProviderHandle = AbilityCostProviderCatalog::Handle;
