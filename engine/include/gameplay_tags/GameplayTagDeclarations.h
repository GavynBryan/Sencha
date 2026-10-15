#pragma once

#include <assets/data/DataAssetTypeRegistry.h>
#include <core/metadata/DataSchema.h>

#include <string>
#include <string_view>
#include <vector>

class GameplayTagRegistry;

// Gameplay tag names written down as content. RuntimeContent registers every
// declared name when content is published; declaring gives a name no meaning.

inline constexpr std::string_view kGameplayTagDeclarationsType = "gameplay.tag_declarations";

struct GameplayTagDeclarations
{
    std::vector<std::string> Tags;
    // Names an actor shows to every machine that sees it, not only its owner
    // (GameplayTagRegistry::SetReplicatedToAll); each is registered too.
    std::vector<std::string> ReplicatedToAll;
};

void RegisterGameplayTagDeclarations(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas);

// Registers `declarations`' names into `tags` and marks the ones replicated to
// all; a name already registered is fine. Appends one message per name the
// registry refuses.
void DeclareGameplayTags(const GameplayTagDeclarations& declarations, GameplayTagRegistry& tags,
                         std::vector<std::string>& errors);
