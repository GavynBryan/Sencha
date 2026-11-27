#pragma once

#include <string>
#include <string_view>

class World;

// Every live verb, query and event `provider` declared in `names` ("" for every
// provider), as a Markdown reference in name order. docs/gameplay/authored-api.md
// describes the layout.
[[nodiscard]] std::string RenderVocabularyReference(const World& names, std::string_view provider);
