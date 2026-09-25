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

struct AnimBoundFactSlot
{
    std::string Name;
    AnimFactKind Kind = AnimFactKind::Float;
    bool Local = false;
    // The derivation writing this slot, or -1 when gameplay gathers it.
    int Derivation = -1;
    // The provider filling this slot in this World, or -1 to keep its given value.
    int Provider = -1;
    // The schema asset and the field path of the declaration.
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
    // Per skeleton joint, 1 where the layer applies; empty when unmasked.
    std::vector<std::uint8_t> Mask;
    [[nodiscard]] bool Masked() const { return !Mask.empty(); }
    [[nodiscard]] bool Covers(std::size_t joint) const { return Mask.empty() || (joint < Mask.size() && Mask[joint] != 0); }
};

// Resolved by key against the rig's binding set at each dispatch. A lifecycle
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
    std::vector<AuthoredValue> Inputs;
    // An unresolved event is kept so a crossing still reports why nothing ran.
    bool Resolved = false;
};

struct AnimBoundBehavior
{
    GameplayTagId Tag;
    std::string Name;
    AnimBehaviorDecl Policy;
    std::vector<GameplayTagId> InterruptTags;
    GameplayTagId SyncGroup;
    std::string DeclaredIn;
    std::optional<AnimBoundEvent> Entered;
    std::optional<AnimBoundEvent> Exited;
};

// One per selector a flattened rule nests through, outermost first.
struct AnimRuleSource
{
    std::string Selector;
    std::uint32_t Rule = 0;
    std::string Name;
};

// Where one row of a flattened predicate was authored.
struct AnimRowSource
{
    std::string Selector;
    std::uint32_t Rule = 0;
    bool Stay = false;
    std::uint32_t Row = 0;
};

struct AnimBoundRule
{
    // The outermost ancestor rule's priority, which a delegated rule inherits.
    std::int32_t PriorityBand = 0;
    AnimProgram Enter;
    AnimProgram Stay;
    bool HasStay = false;
    GameplayTagId Behavior;
    int BehaviorIndex = -1;
    float HoldMinMs = 0.0f;
    float CooldownMs = 0.0f;
    int CooldownSlot = -1;
    // The single intent its enter reads, which an until-request-ends latch follows;
    // invalid when it reads none or several.
    GameplayTagId LatchIntent;
    // Every path to this rule requires a request. A behavior needs this before it
    // may be reconstructed, move the character, or play a looping flow.
    bool RequiresRequest = false;
    // Stable across rebinds (selector path plus rule name or position), so a reload
    // that reorders named rules remaps selector state instead of resetting it.
    std::uint32_t Key = 0;
    std::vector<AnimRuleSource> Source;
    // One per row of Enter and of Stay, in program order.
    std::vector<AnimRowSource> EnterRows;
    std::vector<AnimRowSource> StayRows;
    std::string Label;
};

// The first weight rule whose enter passes sets the layer's weight this tick.
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
    // Re-evaluated every tick rather than only when facts or requests change.
    bool ReadsTime = false;
    bool ReadsTags = false;
};

struct AnimBoundContent
{
    std::string Path;
    // Unset for a flow or blendspace, whose sections or samples are content entries.
    AnimationClipHandle Clip;
    float DurationSeconds = 0.0f;
    // In clip order: by time, then key.
    std::vector<AnimBoundEvent> Events;
    // Index into AnimBoundRig::Flows, or -1 for a clip.
    int Flow = -1;
    // Index into AnimBoundRig::Blendspaces, or -1.
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
    // Index into AnimBoundRig::Contents.
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
    std::optional<AnimBoundEvent> Entered;
    std::optional<AnimBoundEvent> Exited;
};

struct AnimBoundFlow
{
    std::string Path;
    std::vector<AnimBoundFlowSection> Sections;
    // The cancel section, or -1 when cancelling ends the flow.
    int Cancel = -1;
    // Bound per section into each section's Entered and Exited.
    std::optional<AnimLifecycleDecl> SectionEnteredDecl;
    std::optional<AnimLifecycleDecl> SectionExitedDecl;
    // Loops or cancels immediately, so only request-driven behaviors may play it:
    // only a request carries the anchor a late joiner needs to find its section.
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

// Replaces the destination behavior's blend for one from-to pair.
struct AnimBoundBlendOverride
{
    GameplayTagId From;
    GameplayTagId To;
    AnimBlendPolicy Policy;
    std::string DeclaredIn;
    std::uint32_t Index = 0;
};

// A World resource set by the `anim.blend.override_cap` cvar.
struct AnimRigLimits
{
    std::uint32_t BlendOverrideCap = 16;
};

struct AnimBoundRig
{
    std::string RigPath;
    bool Valid = false;
    std::vector<AnimDiagnostic> Diagnostics;

    // The skeleton masks index and every played clip must be keyed to.
    std::string SkeletonPath;
    SkeletonHandle Skeleton;
    std::uint32_t JointCount = 0;

    bool HasFacts = false;
    AnimFactCapacity Capacity = AnimFactCapacity::Small;
    std::vector<AnimBoundFactSlot> Slots;
    std::vector<AnimBoundDerivation> Derivations;
    // The longest temporal window: how long until every derived fact is exact.
    float HorizonMs = 0.0f;
    bool HasTemporalDerivations = false;

    bool HasRequestSchema = false;
    std::vector<AnimBoundIntent> Intents;

    std::vector<AnimBoundLayer> Layers;

    std::vector<AnimBoundBehavior> Behaviors;
    std::vector<AnimBoundSelector> Selectors;
    // Priority, then slot map stack order, then row order.
    std::vector<AnimBoundSlotRow> SlotRows;
    std::vector<AnimBoundContent> Contents;
    std::vector<AnimBoundFlow> Flows;
    std::vector<AnimBoundBlendspace> Blendspaces;
    std::vector<AnimBoundBlendOverride> BlendOverrides;

    // Compiled against this World's verb catalog and rebuilt with the binding.
    VerbBindingSet Bindings;

    // Bumped on every rebuild so a held copy can tell it is stale.
    std::uint64_t Generation = 0;
    // What a session has to agree on about this rig; see AnimRigTimingIdentity.
    std::uint64_t TimingIdentity = 0;

    [[nodiscard]] int FindSlot(std::string_view name) const;
    [[nodiscard]] const AnimBoundIntent* FindIntent(GameplayTagId intent) const;
    [[nodiscard]] const AnimBoundBehavior* FindBehavior(GameplayTagId behavior) const;
    [[nodiscard]] int FindBehaviorIndex(GameplayTagId behavior) const;
    // The pair's override, else `to`'s own policy.
    [[nodiscard]] AnimBlendPolicy ResolveBlend(GameplayTagId from, GameplayTagId to,
                                               const AnimBoundBlendOverride** overridden = nullptr) const;
};

// Binds one rig without caching, for tools validating content against a World.
[[nodiscard]] AnimBoundRig BindAnimRig(const DataAssetCache& data,
                                       const AnimationClipCache* clips,
                                       const SkeletonCache* skeletons,
                                       DataAssetHandle rig,
                                       const World& world);

// A World resource the host publishes with its data cache and replaces with an
// empty one before that cache goes away. Unpublished, it resolves nothing.
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

    // Rebinds first if anything it was bound from changed. Holds no lease: the
    // entities naming the rig keep it resident, and a reused slot has a new generation.
    [[nodiscard]] const AnimBoundRig* Resolve(DataAssetHandle rig, const World& world);

    void Clear() { Entries.clear(); }
    [[nodiscard]] const AnimationClipCache* ClipSource() const { return Clips; }
    [[nodiscard]] const SkeletonCache* SkeletonSource() const { return Skeletons; }
    [[nodiscard]] std::uint64_t RebuildCount() const { return Rebuilds; }
    // Every rig bound so far, as last bound; for reports, in no fixed order.
    template <typename Visit>
    void ForEachBound(Visit&& visit) const
    {
        for (const auto& [key, entry] : Entries)
            visit(entry.Bound);
    }

private:
    struct Entry
    {
        AnimBoundRig Bound;
        // Every asset the binding read, and the reload version it read.
        std::vector<std::pair<DataAssetHandle, std::uint64_t>> Versions;
        // A clip's events are bound here, so a clip replaced in place rebinds the rig.
        std::vector<std::pair<AnimationClipHandle, std::uint64_t>> ClipVersions;
        std::size_t TagCount = 0;
        std::uint64_t ProviderRevision = 0;
        // A catalog replaced or grown since may now resolve a failed event.
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
