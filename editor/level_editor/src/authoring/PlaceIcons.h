#pragma once

#include "icons/IconId.h"
#include "problems/ProblemList.h"

#include <vector>

class ComponentSerializerRegistry;
class IComponentSerializer;

// The icon a component is placed by, as it declares it (SENCHA_PLACE_ICON); None
// when it declares none, or one no icon is named.
[[nodiscard]] IconId PlaceIconOf(const IComponentSerializer& serializer);

// A problem for each component whose declared place icon names no icon: it is
// placed and marked with none, and its author would not otherwise know.
[[nodiscard]] std::vector<Problem> UnknownPlaceIcons(const ComponentSerializerRegistry& serializers);
