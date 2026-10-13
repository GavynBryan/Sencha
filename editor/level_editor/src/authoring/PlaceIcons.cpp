#include "authoring/PlaceIcons.h"

#include "icons/IconNames.h"

#include <world/serialization/ComponentSerializerRegistry.h>

#include <format>
#include <string>

IconId PlaceIconOf(const IComponentSerializer& serializer)
{
    return IconByName(serializer.Placement().Icon);
}

std::vector<Problem> UnknownPlaceIcons(const ComponentSerializerRegistry& serializers)
{
    std::vector<Problem> problems;
    for (const auto& serializer : serializers.Entries())
    {
        const ComponentPlacementFacts facts = serializer->Placement();
        if (facts.Icon.empty() || PlaceIconOf(*serializer) != IconId::None)
            continue;
        problems.push_back({ .Severity = ProblemSeverity::Warning,
                             .Message = std::format("{} is placed by the icon \"{}\", which is not one of the "
                                                    "editor's icons, so it shows none.",
                                                    facts.Label.empty() ? serializer->JsonKey() : facts.Label,
                                                    facts.Icon),
                             .Location = std::string(serializer->JsonKey()) });
    }
    return problems;
}
