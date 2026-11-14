#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

//=============================================================================
// TargetTextureTable
//
// Textures a render feature draws at runtime, which materials name as
// `target:NAME`: a screen on a prop showing authored UI, and later a camera feed
// or a scope. The writer and the materials that sample it never meet; they agree
// on the name alone. Docs: docs/renderer/resources.md, "Target textures".
//
// A material cannot hold a target's bindless index, because the index is the
// writer's render target's, and that changes whenever the target is rebuilt.
// Rewriting a descriptor the material already samples is not legal while an
// earlier frame that used it is still executing, and a material drawn every
// frame always has one. So a material slot holds an encoded target id instead
// (`EncodeSlot`), and the forward pass resolves it per draw (`Resolve`) to
// whatever the writer published this frame. An ordinary index passes through.
//
// Names are interned for the table's lifetime. A name costs one entry whether
// or not anything writes it, and authored content bounds how many exist.
//=============================================================================

inline constexpr std::string_view kTargetTextureScheme = "target:";

struct TargetTextureId
{
    std::uint32_t Value = 0;

    [[nodiscard]] bool IsValid() const { return Value != 0; }
    friend bool operator==(TargetTextureId, TargetTextureId) = default;
};

// The name a `target:NAME` reference gives; nothing for any other path, or for
// the scheme with no name after it.
[[nodiscard]] std::optional<std::string_view> ParseTargetTextureRef(std::string_view path);

class TargetTextureTable
{
public:
    // Bindless indices stay far below this bit, and no index has it set except
    // the "no texture" value, which Resolve leaves alone.
    static constexpr std::uint32_t kSlotFlag = 0x8000'0000u;
    static constexpr std::uint32_t kNoTexture = UINT32_MAX;

    [[nodiscard]] TargetTextureId Intern(std::string_view name);
    [[nodiscard]] TargetTextureId Find(std::string_view name) const;
    [[nodiscard]] std::string_view NameOf(TargetTextureId id) const;

    // What a material texture slot stores to sample the target.
    [[nodiscard]] static std::uint32_t EncodeSlot(TargetTextureId id);
    [[nodiscard]] static bool IsTargetSlot(std::uint32_t slot);

    // The bindless index a material slot samples this frame: a target's
    // published index, the unpublished texture's while nothing writes it, and
    // any other slot value unchanged.
    [[nodiscard]] std::uint32_t Resolve(std::uint32_t slot) const;

    // Called by the writer each frame its target holds this frame's pixels,
    // before anything that samples it records.
    void Publish(TargetTextureId id, std::uint32_t bindlessIndex);
    void Withdraw(TargetTextureId id);
    [[nodiscard]] bool IsPublished(TargetTextureId id) const;

    // What an unwritten target samples: a black texture, so a screen nothing
    // draws is off rather than lit by the slot's neutral default.
    void SetUnpublishedIndex(std::uint32_t bindlessIndex) { Unpublished = bindlessIndex; }
    [[nodiscard]] std::uint32_t UnpublishedIndex() const { return Unpublished; }

    [[nodiscard]] std::size_t Count() const { return Entries.size(); }

private:
    struct Entry
    {
        std::string Name;
        std::uint32_t Published = kNoTexture;
    };

    std::vector<Entry> Entries;
    std::uint32_t Unpublished = kNoTexture;
};
