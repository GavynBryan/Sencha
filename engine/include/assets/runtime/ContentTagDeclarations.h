#pragma once

#include <string>
#include <vector>

class GameplayTagRegistry;
struct RuntimeAssets;

// Every name the mounted `gameplay.tag_declarations` assets list, in asset path
// order and then list order. Appends one message per declaration that fails.
void CollectContentTags(RuntimeAssets& assets, std::vector<std::string>& names, std::vector<std::string>& errors);

// Registers those names; also appends one message per name refused.
void DeclareContentTags(RuntimeAssets& assets, GameplayTagRegistry& tags, std::vector<std::string>& errors);
