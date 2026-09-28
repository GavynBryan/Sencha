#pragma once

#include <authored/AuthoredCatalog.h>
#include <authored/AuthoredQueryRegistry.h>
#include <authored/AuthoredSchema.h>
#include <spatial/candidates/CandidateTypes.h>
#include <core/identity/StrongId.h>
#include <core/json/JsonValue.h>
#include <core/metadata/DataSchema.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

class CandidateRun;
class GameplayTagRegistry;

// Named operations definitions refer to. Prepare runs with no World at load;
// Bind resolves World vocabulary on the owner thread. docs/spatial/candidates.md.

// What Prepare may resolve: slot names, in the definition's order.
struct CandidatePrepareContext
{
    std::span<const std::string> SlotNames;

    // Slot 0 is the querier, named "querier".
    [[nodiscard]] std::optional<std::uint8_t> FindSlot(std::string_view name) const;
};

struct CandidateBindEnvironment
{
    const GameplayTagRegistry* Tags = nullptr;
    const AuthoredQueryRegistry* Queries = nullptr;
};

struct CandidatePrepared
{
    std::shared_ptr<const void> State;
    // Overrides the operation's declared class when its arguments change its cost.
    std::optional<CandidateCostClass> Cost;
};

// `arguments` has already passed the operation's schema. Messages start with `subject`.
using CandidatePrepareFn = CandidatePrepared (*)(const JsonValue& arguments,
                                                 const CandidatePrepareContext& context,
                                                 std::string_view subject,
                                                 std::vector<std::string>& errors);
// Null state with no new error means "use the prepared state as is".
using CandidateBindFn = std::shared_ptr<const void> (*)(const void* prepared,
                                                        const CandidateBindEnvironment& environment,
                                                        std::string_view subject,
                                                        std::vector<std::string>& errors);
// Writes values[row] and statuses[row] for every row it is given.
using CandidateMeasureFn = void (*)(CandidateRun& run,
                                    const void* state,
                                    std::span<const std::uint32_t> rows,
                                    std::span<float> values,
                                    std::span<CandidateMeasureStatus> statuses);
using CandidateGenerateFn = void (*)(CandidateRun& run, const void* state);

struct CandidateMeasureDefinition
{
    std::string Name;
    std::string DisplayName;
    std::string Description;
    DataFieldSchema Arguments = AuthoredRecordRoot();
    // Lets members the schema does not list through to Prepare.
    bool OpenArguments = false;

    CandidateCostClass Cost = CandidateCostClass::Geometric;
    CandidateAppliesTo AppliesTo = CandidateAppliesTo::Any;
    CandidateValueKind Value = CandidateValueKind::Scalar;

    CandidatePrepareFn Prepare = nullptr;
    CandidateBindFn Bind = nullptr;
    CandidateMeasureFn Measure = nullptr;
};

struct CandidateGeneratorDefinition
{
    std::string Name;
    std::string DisplayName;
    std::string Description;
    DataFieldSchema Arguments = AuthoredRecordRoot();

    CandidatePrepareFn Prepare = nullptr;
    CandidateBindFn Bind = nullptr;
    CandidateGenerateFn Generate = nullptr;
};

using CandidateMeasureId = StrongId<struct CandidateMeasureIdTag, std::uint32_t>;
using CandidateMeasureRevision = StrongId<struct CandidateMeasureRevisionTag, std::uint32_t>;
using CandidateMeasureCatalogId = StrongId<struct CandidateMeasureCatalogTag, std::uint64_t>;
using CandidateGeneratorId = StrongId<struct CandidateGeneratorIdTag, std::uint32_t>;
using CandidateGeneratorRevision = StrongId<struct CandidateGeneratorRevisionTag, std::uint32_t>;
using CandidateGeneratorCatalogId = StrongId<struct CandidateGeneratorCatalogTag, std::uint64_t>;

struct CandidateMeasureCatalogTraits
{
    using Definition = CandidateMeasureDefinition;
    using Id = CandidateMeasureId;
    using Revision = CandidateMeasureRevision;
    using CatalogId = CandidateMeasureCatalogId;
    static constexpr std::string_view Noun = "measure";

    static void Validate(const CandidateMeasureDefinition& definition, std::vector<std::string>& errors);

    static bool ContractsMatch(const CandidateMeasureDefinition& left, const CandidateMeasureDefinition& right)
    {
        return AuthoredContractsMatch(left.Arguments, right.Arguments)
            && left.OpenArguments == right.OpenArguments && left.Cost == right.Cost
            && left.AppliesTo == right.AppliesTo && left.Value == right.Value;
    }
};

struct CandidateGeneratorCatalogTraits
{
    using Definition = CandidateGeneratorDefinition;
    using Id = CandidateGeneratorId;
    using Revision = CandidateGeneratorRevision;
    using CatalogId = CandidateGeneratorCatalogId;
    static constexpr std::string_view Noun = "generator";

    static void Validate(const CandidateGeneratorDefinition& definition, std::vector<std::string>& errors);

    static bool ContractsMatch(const CandidateGeneratorDefinition& left, const CandidateGeneratorDefinition& right)
    {
        return AuthoredContractsMatch(left.Arguments, right.Arguments);
    }
};

using CandidateMeasureCatalog = AuthoredCatalog<CandidateMeasureCatalogTraits>;
using CandidateMeasureRegistrationScope = AuthoredRegistrationScope<CandidateMeasureCatalogTraits>;
using CandidateMeasureHandle = CandidateMeasureCatalog::Handle;
using CandidateGeneratorCatalog = AuthoredCatalog<CandidateGeneratorCatalogTraits>;
using CandidateGeneratorRegistrationScope = AuthoredRegistrationScope<CandidateGeneratorCatalogTraits>;
using CandidateGeneratorHandle = CandidateGeneratorCatalog::Handle;

// The operations one host offers. Built with the engine's operations and
// read-only afterwards, so definitions may compile against it from any thread.
class CandidateCatalogs
{
public:
    CandidateCatalogs();

    CandidateCatalogs(const CandidateCatalogs&) = delete;
    CandidateCatalogs& operator=(const CandidateCatalogs&) = delete;

    [[nodiscard]] const CandidateMeasureCatalog& Measures() const { return Measures_; }
    [[nodiscard]] const CandidateGeneratorCatalog& Generators() const { return Generators_; }

private:
    CandidateMeasureCatalog Measures_;
    CandidateGeneratorCatalog Generators_;
};

// The engine's operations, declared into one batch per catalog.
void DeclareBuiltInCandidateOperations(CandidateMeasureRegistrationScope& measures,
                                       CandidateGeneratorRegistrationScope& generators);
