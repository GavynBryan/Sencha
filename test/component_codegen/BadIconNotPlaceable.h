#pragma once

#include <ecs/ComponentAnnotations.h>

// An icon pictures a placeable component; without the label nothing shows it.
struct SENCHA_COMPONENT("test.codegen.bad_icon")
       SENCHA_PLACE_ICON("lamp")
BadIconNotPlaceable
{
};
