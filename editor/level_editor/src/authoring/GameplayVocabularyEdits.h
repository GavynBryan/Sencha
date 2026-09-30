#pragma once

#include <abilities/AbilityId.h>
#include <attributes/AttributeId.h>
#include <gameplay_tags/GameplayTagId.h>

#include <cstdint>
#include <string>
#include <string_view>

struct AbilitySet;
struct AttributeSet;
struct CharacterMovement;
struct GameplayTagContainer;
class AbilityRegistry;
class AttributeRegistry;
class GameplayTagRegistry;
class LocomotionModeRegistry;

//=============================================================================
// Gameplay vocabulary edits
//
// The components whose values are registry-resolved names rather than numbers
// -- tags, attributes, abilities, and the locomotion mode -- edited as plain
// values against the registries that define them.
//
// They live here rather than inside a draw call for two reasons. The rules are
// real (an attribute's base clamps to its registered range; a tag name that is
// not yet registered is declared rather than dropped; every one of these
// containers has a capacity), and a rule inside a widget is a rule that cannot
// be tested without an ImGui context. An inspector row is a widget over one of
// these calls.
//=============================================================================

// The outcome of an edit that can be refused. `Error` is empty unless it was,
// and is the text to put in front of whoever asked for it.
struct VocabularyEdit
{
    bool        Changed = false;
    std::string Error;
};

// Grant `stacks` of the named tag, registering the name first if the registry
// does not have it: the tag vocabulary is open, so a designer typing a new
// dot-path is declaring it rather than making a mistake. The scene stores the
// name, and a process whose registry lacks it skips the tag on load.
[[nodiscard]] VocabularyEdit GrantTagByName(GameplayTagContainer& tags,
                                            GameplayTagRegistry& registry,
                                            std::string_view name,
                                            std::uint16_t stacks = 1);

// Set a held tag's stack count outright, rather than adding to it. Zero
// revokes the tag, which is what a count of zero already means.
[[nodiscard]] VocabularyEdit SetTagStacks(GameplayTagContainer& tags,
                                          GameplayTagId tag,
                                          std::uint16_t stacks);

[[nodiscard]] VocabularyEdit RevokeTag(GameplayTagContainer& tags, GameplayTagId tag);

// Add an attribute at the base value its registration declares, which is the
// value the entity would have had if nothing authored it.
[[nodiscard]] VocabularyEdit AddAttribute(AttributeSet& set,
                                          const AttributeRegistry& registry,
                                          AttributeId id);

// Set an attribute's base, clamped to its registered range. The range is part
// of what the attribute means, so a value outside it is not an authored
// choice -- the resolve pass would clamp it away on the first tick anyway.
//
// Current is not touched: it is derived from Base and the active modifiers by
// ResolveAttributes, and is neither authored nor persisted.
[[nodiscard]] VocabularyEdit SetAttributeBase(AttributeSet& set,
                                              const AttributeRegistry& registry,
                                              AttributeId id,
                                              float base);

[[nodiscard]] VocabularyEdit RemoveAttribute(AttributeSet& set, AttributeId id);

[[nodiscard]] VocabularyEdit GrantAbility(AbilitySet& set,
                                          const AbilityRegistry& registry,
                                          AbilityId id);

[[nodiscard]] VocabularyEdit RevokeAbility(AbilitySet& set, AbilityId id);

// Put the character in the named mode. An empty or unknown name is the free
// mode, which is what the scene form already means by an absent mode.
[[nodiscard]] VocabularyEdit SetLocomotionMode(CharacterMovement& movement,
                                               const LocomotionModeRegistry& modes,
                                               std::string_view name);
