#pragma once

#include "workspaces/facets/TimelineFacet.h"

#include <core/json/JsonValue.h>

#include <string_view>

class AbilityPreviewSession;

// The preview's history along the deck, in seconds of its fixed ticks: a span
// per stay of each activation in a phase, a mark per input the script gave and
// per operation that did not accept, and `plotted` as a plot. `data` names phases
// and ops; null leaves them by key.
[[nodiscard]] WorkspaceTimelineFacet AbilityPreviewTimeline(const AbilityPreviewSession& preview, const JsonValue* data,
                                                            std::string_view plotted);
