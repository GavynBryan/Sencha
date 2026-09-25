#pragma once

#include <assets/data/DataAssetTypeRegistry.h>
#include <core/metadata/DataSchema.h>

#include <string>
#include <string_view>
#include <vector>

class GameplayTagRegistry;

//=============================================================================
// Gameplay tag declarations
//
// A project's gameplay tags written down as content rather than code: a
// `gameplay.tag_declarations` data asset lists names, and the runtime
// registers every name every such asset lists when content is published --
// before any content binds, in asset path order, once. A game module still
// declares its own through its vocabulary hook; content that needs names no
// module knows (an animation rig made in the editor, say) ships them here.
//
// Declaring is all it does: a name is registered, not given meaning.
//=============================================================================

inline constexpr std::string_view kGameplayTagDeclarationsType = "gameplay.tag_declarations";

struct GameplayTagDeclarations
{
    std::vector<std::string> Tags;
};

void RegisterGameplayTagDeclarations(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas);

// Registers `declarations`' names into `tags`; a name already registered is
// fine. Appends one message per name the registry refuses.
void DeclareGameplayTags(const GameplayTagDeclarations& declarations, GameplayTagRegistry& tags,
                         std::vector<std::string>& errors);
