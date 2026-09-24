#pragma once

#include <anim/AnimBehaviorSet.h>
#include <anim/AnimDiagnostic.h>
#include <anim/AnimFactSchema.h>
#include <anim/AnimFlowData.h>
#include <anim/AnimPredicate.h>
#include <anim/AnimRequestSchema.h>
#include <anim/AnimRigData.h>
#include <anim/AnimationClip.h>
#include <anim/AnimationClipHandle.h>
#include <anim/SkeletonHandle.h>
#include <assets/data/DataAssetCache.h>
#include <authored/VerbBindingSet.h>
#include <gameplay_tags/GameplayTagId.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class AnimationClipCache;
class SkeletonCache;
class World;

//=============================================================================
// AnimRigBinding
//
// A rig asset, bound into one World: the fact schema chain merged into one
// fixed layout, derivations compiled to slot indices, layer and intent names
// resolved to this World's tag ids, gathered slots matched to this World's
// providers, each layer's selector flattened and compiled, behaviors resolved
// to their policies, the slot map stack merged into rows over a content
// table, and every clip event on that content bound to the rig's authored
// bindings. The shared assets name things; this is where names become the
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
    // Index into AnimBoundRig::Selectors, or -1 for a request-keyed layer.
    int Selector = -1;
    GameplayTagId Idle;
    // One entry per joint of the rig's skeleton, 1 where the layer applies;
    // empty for an unmasked layer.
    std::vector<std::uint8_t> Mask;
    [[nodiscard]] bool Masked() const { return !Mask.empty(); }
    [[nodiscard]] bool Covers(std::size_t joint) const { return Mask.empty() || (joint < Mask.size() && Mask[joint] != 0); }
};

// One clip or lifecycle event, bound: its binding named by key and its inputs
// already converted to values, in the binding's input order, against this
// World's catalog. Holds no pointer to the compiled binding; the event pass
// looks it up by key in the rig's binding set at each dispatch. A lifecycle
// event has no key or time.
struct AnimBoundEvent
{
    std::uint32_t Key = 0;
    // Normalized clip time.
    float Time = 0.0f;
    AnimEventScope Scope = AnimEventScope::Cosmetic;
    std::optional<float> MinWeight;
    VerbBindingKey Binding;
    std::string BindingText;
    std::vector<VerbValue> Inputs;
    // The binding resolved and every input converted. An unresolved event is
    // kept, so a crossing still reports why nothing was invoked.
    bool Resolved = false;
};

struct AnimBoundBehavior
{
    GameplayTagId Tag;
    std::string Name;
    AnimBehaviorDecl Policy;
    std::vector<GameplayTagId> InterruptTags;
    GameplayTagId SyncGroup;
    // The behavior set whose declaration won, for navigation.
    std::string DeclaredIn;
    // Its lifecycle events, bound like clip events; absent when not declared.
    std::optional<AnimBoundEvent> Entered;
    std::optional<AnimBoundEvent> Exited;
};

// Where a flattened rule came from: one entry per selector it nests through,
// outermost first.
struct AnimRuleSource
{
    std::string Selector;
    std::uint32_t Rule = 0;
    std::string Name;
};

// Where one row of a flattened predicate was authored: which selector, which
// rule in it, which of its predicates, which row. A failing row index names a
// row the author can find, however deeply the rule was delegated.
struct AnimRowSource
{
    std::string Selector;
    std::uint32_t Rule = 0;
    bool Stay = false;
    std::uint32_t Row = 0;
};

struct AnimBoundRule
{
    // The priority of the outermost rule it descends from: what holds and
    // latches compare, since a delegated rule inherits its parent's standing.
    std::int32_t Band = 0;
    AnimProgram Enter;
    // Empty when the rule stays on its enter.
    AnimProgram Stay;
    bool HasStay = false;
    GameplayTagId Behavior;
    int BehaviorIndex = -1;
    float HoldMinMs = 0.0f;
    float CooldownMs = 0.0f;
    // Which cooldown slot of the selector state it uses, or -1.
    int CooldownSlot = -1;
    // The one intent its enter reads, which is the request an
    // until-request-ends latch follows. Invalid when it reads none or several.
    GameplayTagId LatchIntent;
    // No entity passes its enter, or any delegating rule's, without a request:
    // what a behavior needs of every rule reaching it before it may be
    // reconstructed, move the character, or play a flow that loops.
    bool RequiresRequest = false;
    // Stable across rebinds: the selector path and the rule's name (or its
    // position when unnamed) through every level it nests. Selector state
    // names its winner by this, so a reload that reorders named rules remaps
    // rather than resets.
    std::uint32_t Key = 0;
    std::vector<AnimRuleSource> Source;
    // One per row of Enter and of Stay, in program order.
    std::vector<AnimRowSource> EnterRows;
    std::vector<AnimRowSource> StayRows;
    std::string Label;
};

// A rule that weights its layer: the first in evaluation order whose enter
// passes decides the weight this tick.
struct AnimBoundWeightRule
{
    AnimProgram Enter;
    float Value = 1.0f;
    // A float fact slot read instead of Value, or -1.
    int FactSlot = -1;
    std::uint32_t Key = 0;
    std::vector<AnimRuleSource> Source;
    std::vector<AnimRowSource> EnterRows;
    std::string Label;
};

struct AnimBoundSelector
{
    std::string Path;
    // Flattened, in evaluation order.
    std::vector<AnimBoundRule> Rules;
    std::vector<AnimBoundWeightRule> WeightRules;
    // Some rule reads time or tags: the selector re-evaluates every tick
    // rather than only when facts or requests change.
    bool ReadsTime = false;
    bool ReadsTags = false;
};

struct AnimBoundContent
{
    std::string Path;
    // A clip, or -- when Flow is set -- no clip: a flow's content is its
    // sections', each of which is a content entry of its own.
    AnimationClipHandle Clip;
    float DurationSeconds = 0.0f;
    // In clip order: by time, then key.
    std::vector<AnimBoundEvent> Events;
    // Index into AnimBoundRig::Flows, or -1 for a clip.
    int Flow = -1;
    // Index into AnimBoundRig::Blendspaces, or -1. A blendspace's content is
    // its samples', each a content entry of its own.
    int Blendspace = -1;

    [[nodiscard]] bool IsClip() const { return Flow < 0 && Blendspace < 0; }
};

struct AnimBoundBlendspaceAxis
{
    std::string Fact;
    int FactSlot = -1;
    float Min = 0.0f;
    float Max = 1.0f;
};

struct AnimBoundBlendspaceSample
{
    // The sample's clip, as a content entry.
    int Content = -1;
    float At[2] = {};
};

struct AnimBoundBlendspace
{
    std::string Path;
    std::uint8_t AxisCount = 1;
    AnimBoundBlendspaceAxis Axes[2];
    std::vector<AnimBoundBlendspaceSample> Samples;
};

struct AnimBoundFlowBranch
{
    AnimProgram When;
    std::uint8_t To = 0;
};

struct AnimBoundFlowSection
{
    GameplayTagId Tag;
    std::string TagName;
    // The clip's content index; -1 for a slot section, resolved on entry.
    int Content = -1;
    GameplayTagId Slot;
    AnimFlowLoop Loop = AnimFlowLoop::Once;
    AnimProgram While;
    // Count loops: the request and the parameter slot giving the count.
    GameplayTagId CountIntent;
    int CountParam = -1;
    std::vector<AnimBoundFlowBranch> Branches;
    bool Ends = false;
    AnimCancelTiming CancelTiming = AnimCancelTiming::AtSectionEnd;
    // The flow's section lifecycle events, bound with this section's tag.
    std::optional<AnimBoundEvent> Entered;
    std::optional<AnimBoundEvent> Exited;
};

struct AnimBoundFlow
{
    std::string Path;
    std::vector<AnimBoundFlowSection> Sections;
    // The cancel section, or -1 when cancelling ends the flow.
    int Cancel = -1;
    // As declared; bound per section, with the section's tag, into Entered
    // and Exited once the rig's bindings are compiled.
    std::optional<AnimLifecycleDecl> SectionEnteredDecl;
    std::optional<AnimLifecycleDecl> SectionExitedDecl;
    // A loop or an immediate cancel: only a behavior every path to which runs
    // through a request may play it, because only a request carries the anchor
    // a late joiner needs to find its section.
    bool NeedsRequest = false;
};

struct AnimBoundSlotRow
{
    GameplayTagId Behavior;
    std::string BehaviorName;
    std::int32_t Priority = 0;
    AnimProgram When;
    int Content = -1;
    std::string DeclaredIn;
    std::uint32_t Index = 0;
    std::uint32_t Key = 0;
};

// A pairwise blend policy, bound: the change from one behavior to another
// blends by Policy instead of the destination's own.
struct AnimBoundBlendOverride
{
    GameplayTagId From;
    GameplayTagId To;
    AnimBlendPolicy Policy;
    // The asset and entry that won, for navigation.
    std::string DeclaredIn;
    std::uint32_t Index = 0;
};

// Limits content binds against: a World resource the animation vocabulary
// installs and the `anim.blend.override_cap` cvar sets.
struct AnimRigLimits
{
    std::uint32_t BlendOverrideCap = 16;
};

struct AnimBoundRig
{
    std::string RigPath;
    bool Valid = false;
    std::vector<AnimDiagnostic> Diagnostics;

    // The skeleton the rig poses, when it declares one: what masks name
    // joints of and every clip it plays must be keyed to.
    std::string SkeletonPath;
    SkeletonHandle Skeleton;
    std::uint32_t JointCount = 0;

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

    std::vector<AnimBoundBehavior> Behaviors;
    std::vector<AnimBoundSelector> Selectors;
    // Merged across the slot map stack: priority first, then stack order,
    // then row order.
    std::vector<AnimBoundSlotRow> SlotRows;
    std::vector<AnimBoundContent> Contents;
    std::vector<AnimBoundFlow> Flows;
    std::vector<AnimBoundBlendspace> Blendspaces;
    std::vector<AnimBoundBlendOverride> BlendOverrides;

    // The rig's authored bindings, compiled against this World's catalog.
    // Rebuilt with the rest of the binding, never refreshed in place.
    VerbBindingSet Bindings;

    // Moves on every rebuild, so an inspector holding a copy can tell it is
    // looking at an older generation.
    std::uint64_t Generation = 0;

    [[nodiscard]] int FindSlot(std::string_view name) const;
    [[nodiscard]] const AnimBoundIntent* FindIntent(GameplayTagId intent) const;
    [[nodiscard]] const AnimBoundBehavior* FindBehavior(GameplayTagId behavior) const;
    [[nodiscard]] int FindBehaviorIndex(GameplayTagId behavior) const;
    // How a layer changing from `from` to `to` blends: the pair's override,
    // or `to`'s own policy. Null `overridden` when the caller does not ask.
    [[nodiscard]] AnimBlendPolicy ResolveBlend(GameplayTagId from, GameplayTagId to,
                                               const AnimBoundBlendOverride** overridden = nullptr) const;
};

// Binds one rig without caching. The pure half of AnimRigBindings, and what a
// tool validating content against a World calls directly.
[[nodiscard]] AnimBoundRig BindAnimRig(const DataAssetCache& data,
                                       const AnimationClipCache* clips,
                                       const SkeletonCache* skeletons,
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
    AnimRigBindings(const DataAssetCache* data, const AnimationClipCache* clips,
                    const SkeletonCache* skeletons = nullptr)
        : Data(data)
        , Clips(clips)
        , Skeletons(skeletons)
    {
    }

    // The rig bound into `world`, rebuilt first if anything it was bound from
    // has changed. Null for an invalid handle or a value that is not a rig.
    // The binding holds no lease: the entities naming the rig keep it
    // resident, the rig's data entry holds its schemas, and an entry for a
    // freed rig is unreachable because a reused slot carries a new generation.
    [[nodiscard]] const AnimBoundRig* Resolve(DataAssetHandle rig, const World& world);

    void Clear() { Entries.clear(); }
    // The caches content is bound from, for what poses it.
    [[nodiscard]] const AnimationClipCache* ClipSource() const { return Clips; }
    [[nodiscard]] const SkeletonCache* SkeletonSource() const { return Skeletons; }
    [[nodiscard]] std::uint64_t RebuildCount() const { return Rebuilds; }

private:
    struct Entry
    {
        AnimBoundRig Bound;
        // Every asset the binding read, and the reload version it read.
        std::vector<std::pair<DataAssetHandle, std::uint64_t>> Versions;
        // Every clip its content plays, likewise: a clip's events are bound
        // here, so a clip replaced in place rebinds the rig.
        std::vector<std::pair<AnimationClipHandle, std::uint64_t>> ClipVersions;
        std::size_t TagCount = 0;
        std::uint64_t ProviderRevision = 0;
        // The verb catalog the bindings compiled against: a catalog replaced
        // or grown since means an event that failed to resolve may now.
        VerbCatalogId Catalog;
        std::uint64_t CatalogGeneration = 0;
        std::uint32_t BlendOverrideCap = 0;
    };

    [[nodiscard]] bool IsCurrent(const Entry& entry, const World& world) const;

    const DataAssetCache* Data = nullptr;
    const AnimationClipCache* Clips = nullptr;
    const SkeletonCache* Skeletons = nullptr;
    std::unordered_map<std::uint64_t, Entry> Entries;
    std::uint64_t Rebuilds = 0;
};
