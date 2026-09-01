#pragma once

#include <core/text/InlineString.h>
#include <ecs/ComponentAnnotations.h>

// The authored display name of an entity. Editor-only, like BrushComponent:
// the hierarchy shows and edits it, the document serializes it, and the level
// cook strips it from the passthrough scene so the runtime schema never has to
// know it. The EditorScene factories stamp a default ("Entity", "Brush 1",
// ...) on everything they mint; only loaded legacy content and projection-
// expanded entities may lack one, and those are labeled by their components.
struct SENCHA_COMPONENT("name")
       SENCHA_SCHEMA("name")
       SENCHA_SCENE_CHUNK("ENAM")
EntityNameComponent
{
    SENCHA_FIELD("value")
    InlineString<64> Value;
};

#if !defined(SENCHA_CODEGEN)
#  include "document/EntityNameComponent.sencha.h"
#endif
