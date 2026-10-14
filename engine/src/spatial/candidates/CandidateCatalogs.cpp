#include <spatial/candidates/CandidateCatalogs.h>

#include <cassert>
#include <format>

namespace
{
    void ValidateArguments(std::string_view name, const DataFieldSchema& arguments, std::vector<std::string>& errors)
    {
        if (arguments.Kind != DataFieldKind::Record)
            errors.push_back(std::format("'{}': the argument root is a record", name));
        else
            ValidateAuthoredField(arguments, std::string(name), errors);
    }
}

std::optional<std::uint8_t> CandidatePrepareContext::FindSlot(std::string_view name) const
{
    for (std::size_t index = 0; index < SlotNames.size(); ++index)
    {
        if (SlotNames[index] == name)
            return static_cast<std::uint8_t>(index);
    }
    return std::nullopt;
}

void CandidateMeasureCatalogTraits::Validate(const CandidateMeasureDefinition& definition,
                                             std::vector<std::string>& errors)
{
    ValidateArguments(definition.Name, definition.Arguments, errors);
    if (definition.Prepare == nullptr || definition.Measure == nullptr)
        errors.push_back(std::format("'{}': a measure needs Prepare and Measure", definition.Name));
}

void CandidateGeneratorCatalogTraits::Validate(const CandidateGeneratorDefinition& definition,
                                               std::vector<std::string>& errors)
{
    ValidateArguments(definition.Name, definition.Arguments, errors);
    if (definition.Prepare == nullptr || definition.Generate == nullptr)
        errors.push_back(std::format("'{}': a generator needs Prepare and Generate", definition.Name));
}

CandidateCatalogs::CandidateCatalogs()
{
    CandidateMeasureRegistrationScope measures(Measures_, "engine");
    CandidateGeneratorRegistrationScope generators(Generators_, "engine");
    DeclareBuiltInCandidateOperations(measures, generators);
    [[maybe_unused]] const bool committed = CommitTogether(measures, generators);
    assert(committed && "the engine's candidate operations must declare cleanly");
}
