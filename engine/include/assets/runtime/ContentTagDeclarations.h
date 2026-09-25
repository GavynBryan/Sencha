#pragma once

#include <string>
#include <vector>

class GameplayTagRegistry;
struct RuntimeAssets;

// Every name every mounted `gameplay.tag_declarations` asset lists
// (GameplayTagDeclarations.h), in asset path order and then list order.
// Appends one message per declaration that does not load.
void CollectContentTags(RuntimeAssets& assets, std::vector<std::string>& names, std::vector<std::string>& errors);

// Registers those names: what the runtime does when content is published.
// Appends one message per declaration that does not load and per name refused.
void DeclareContentTags(RuntimeAssets& assets, GameplayTagRegistry& tags, std::vector<std::string>& errors);
