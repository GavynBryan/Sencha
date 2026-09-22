#pragma once

#include <anim/AnimDiagnostic.h>
#include <anim/AnimFactSchema.h>
#include <anim/AnimRequestSchema.h>
#include <anim/AnimRigData.h>
#include <assets/data/DataAssetCache.h>
#include <gameplay_tags/GameplayTagId.h>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class World;

//=============================================================================
// AnimRigBinding
//
// A rig asset, bound into one World: the fact schema chain merged into one
// fixed layout, derivations compiled to slot indices, layer and intent names
// resolved to this World's tag ids, gathered slots matched to this World's
// providers. The shared assets name things; this is where names become the
// indices per-entity state is addressed by.
//
// Everything wrong with the content is a located AnimDiagnostic, and a rig with
// an error binds as invalid: its entities gather nothing rather than run half a
// layout. The binding is derived state -- rebuilt whenever an asset in its
// chain reloads, the tag vocabulary grows, or a provider is bound -- and no
// component ever holds a pointer into it.
//=============================================================================

struct AnimBoundFactSlot
{
    std::string Name;
    AnimFactKind Kind = AnimFactKind::Float;
    bool Local = false;
    // The derivation that writes this slot, or -1 for a slot gameplay gathers.
    int Derivation = -1;
    // The provider that fills a gathered slot in this World, or -1: nothing
    // publishes it here, and it keeps whatever value it was given.
    int Provider = -1;
    // Where it was declared, for navigation and diagnostics: the schema asset
    // and the field path of the declaration inside it.
    std::string DeclaredIn;
    std::string DeclaredAt;
};

inline constexpr std::size_t kAnimMaxDerivationSources = 8;

struct AnimBoundOperand
{
    std::uint8_t Slot = 0;
    bool Negate = false;
};

struct AnimBoundDerivation
{
    AnimDerivationOp Op = AnimDerivationOp::Edge;
    std::uint8_t Result = 0;
    std::uint8_t SourceCount = 0;
    std::array<AnimBoundOperand, kAnimMaxDerivationSources> Sources{};
    // The kind of the first source, for the ops that read a number.
    AnimFactKind SourceKind = AnimFactKind::Bool;
    bool Rising = true;
    bool MatchValue = false;
    AnimCompareOp Compare = AnimCompareOp::Gt;
    float WindowMs = 0.0f;
    float Enter = 0.0f;
    float Exit = 0.0f;
    float Constant = 0.0f;
};

struct AnimBoundParam
{
    std::string Name;
    AnimRequestParamKind Kind = AnimRequestParamKind::Float;
};

struct AnimBoundIntent
{
    GameplayTagId Intent;
    std::string Name;
    std::vector<AnimBoundParam> Params;
};

struct AnimBoundLayer
{
    GameplayTagId Name;
    std::string NameText;
    AnimLayerMode Mode = AnimLayerMode::Override;
    float Weight = 1.0f;
};

struct AnimBoundRig
{
    std::string RigPath;
    bool Valid = false;
    std::vector<AnimDiagnostic> Diagnostics;

    bool HasFacts = false;
    AnimFactCapacity Capacity = AnimFactCapacity::Small;
    std::vector<AnimBoundFactSlot> Slots;
    std::vector<AnimBoundDerivation> Derivations;
    // The longest temporal window: how long an entity must be observed before
    // every derived fact is exact.
    float HorizonMs = 0.0f;
    bool HasTemporalDerivations = false;

    bool HasRequestSchema = false;
    std::vector<AnimBoundIntent> Intents;

    std::vector<AnimBoundLayer> Layers;

    // Moves on every rebuild, so an inspector holding a copy can tell it is
    // looking at an older generation.
    std::uint64_t Generation = 0;

    [[nodiscard]] int FindSlot(std::string_view name) const;
    [[nodiscard]] const AnimBoundIntent* FindIntent(GameplayTagId intent) const;
};

// Binds one rig without caching. The pure half of AnimRigBindings, and what a
// tool validating content against a World calls directly.
[[nodiscard]] AnimBoundRig BindAnimRig(const DataAssetCache& data,
                                       DataAssetHandle rig,
                                       const World& world);

// The World's bound rigs. A World resource the host publishes with its data
// cache and replaces with an empty one before the cache goes away, so the
// request API, fact gathering and the preview all resolve through one
// generation. Unpublished, it resolves nothing.
class AnimRigBindings
{
public:
    AnimRigBindings() = default;
    explicit AnimRigBindings(const DataAssetCache* data) : Data(data) {}

    // The rig bound into `world`, rebuilt first if anything it was bound from
    // has changed. Null for an invalid handle or a value that is not a rig.
    // The binding holds no lease: the entities naming the rig keep it
    // resident, the rig's data entry holds its schemas, and an entry for a
    // freed rig is unreachable because a reused slot carries a new generation.
    [[nodiscard]] const AnimBoundRig* Resolve(DataAssetHandle rig, const World& world);

    void Clear() { Entries.clear(); }
    [[nodiscard]] std::uint64_t RebuildCount() const { return Rebuilds; }

private:
    struct Entry
    {
        AnimBoundRig Bound;
        // Every asset the binding read, and the reload version it read.
        std::vector<std::pair<DataAssetHandle, std::uint64_t>> Versions;
        std::size_t TagCount = 0;
        std::uint64_t ProviderRevision = 0;
    };

    [[nodiscard]] bool IsCurrent(const Entry& entry, const World& world) const;

    const DataAssetCache* Data = nullptr;
    std::unordered_map<std::uint64_t, Entry> Entries;
    std::uint64_t Rebuilds = 0;
};
