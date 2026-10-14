#pragma once

#include <cstdint>
#include <string>
#include <vector>

class GameplayTagRegistry;
class World;
struct RuntimeAssets;

// The vocabulary content declares: gameplay tags, attributes, effects and abilities, each
// registered into a World's registries in asset path order, so every machine and
// every editor document gets the same names in the same order.

// Every name the mounted `gameplay.tag_declarations` assets list, in asset path
// order and then list order. Appends one message per declaration that fails.
void CollectContentTags(RuntimeAssets& assets, std::vector<std::string>& names, std::vector<std::string>& errors);

// Registers those names; also appends one message per name refused.
void DeclareContentTags(RuntimeAssets& assets, GameplayTagRegistry& tags, std::vector<std::string>& errors);

enum class ContentVocabularyReload : std::uint8_t
{
    // Declared once; nothing is held afterwards.
    Once,
    // Effects and abilities stay leased so a reload can redefine them;
    // ReleaseContentVocabulary lets go, before the asset stack goes away.
    Watch,
};

// Tags, then attributes, then effects, into whichever of `world`'s registries it
// has. An effect is named by its asset path.
void DeclareContentVocabulary(RuntimeAssets& assets, World& world, ContentVocabularyReload reload,
                              std::vector<std::string>& errors);

// Redefines every declared effect whose asset has reloaded since. Existing
// instances keep the timing they were applied with.
void RefreshContentVocabulary(RuntimeAssets& assets, World& world, std::vector<std::string>& errors);

void ReleaseContentVocabulary(World& world);
