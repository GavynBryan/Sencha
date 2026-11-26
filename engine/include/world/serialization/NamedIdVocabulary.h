#pragma once

#include <core/metadata/NamedIdField.h>

#include <cstdint>
#include <string_view>
#include <vector>

class World;
struct IReadArchive;
struct IWriteArchive;
struct SceneSerializationContext;

struct NamedIdVocabulary
{
    std::string_view Label;
    NamedIdKind Kind = NamedIdKind::GameplayTag;
    std::string_view DataSubtype;

    std::string_view (*NameOf)(const World&, std::uint32_t id) = nullptr;
    // Never creates: zero for a name the vocabulary does not hold.
    std::uint32_t (*Find)(const World&, std::string_view name) = nullptr;
    // What a read gives a name: its id, a reserved id, the vocabulary's fallback, or zero.
    std::uint32_t (*Resolve)(World&, std::string_view name) = nullptr;
    void (*Names)(const World&, std::vector<std::string_view>& out) = nullptr;
    // Both null for a vocabulary whose ids two machines of one build share.
    std::uint32_t (*WireKey)(const World&, std::uint32_t id) = nullptr;
    std::uint32_t (*FromWireKey)(const World&, std::uint32_t key) = nullptr;
};

[[nodiscard]] bool SaveNamedId(IWriteArchive& archive, std::string_view key, const NamedIdVocabulary& vocabulary,
                               std::uint32_t id, const World* world, SceneSerializationContext& context);

// An absent key reads as what the vocabulary resolves for no name.
[[nodiscard]] bool LoadNamedId(IReadArchive& archive, std::string_view key, const NamedIdVocabulary& vocabulary,
                               std::uint32_t& id, World* world, SceneSerializationContext& context);
