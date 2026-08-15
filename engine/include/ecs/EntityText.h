#pragma once

#include <ecs/EntityId.h>

#include <string>

// Entity identity is generational, so text that names an entity names both
// halves: printing the index alone lets a recycled slot read as the entity it
// replaced, which is exactly the confusion a diagnostic exists to prevent.
//
// Kept out of EntityId.h so that the <string> dependency lands only on the
// diagnostics that render text, not on every translation unit that handles an
// entity.
void AppendEntityText(std::string& out, EntityId entity);
