#include <anim/AnimRigBinding.h>

#include "AnimRigBinder.h"

#include <anim/AnimFactProviders.h>
#include <ecs/World.h>
#include <gameplay_tags/GameplayTagRegistry.h>

#include <algorithm>
#include <atomic>
#include <format>
#include <utility>

namespace
{
    constexpr std::size_t kMaxSchemaChain = 8;
}

std::uint32_t AnimStableKey(std::string_view text)
{
    std::uint32_t hash = 2166136261u;
    for (const char c : text)
    {
        hash ^= static_cast<std::uint8_t>(c);
        hash *= 16777619u;
    }
    return hash;
}

const GameplayTagRegistry* AnimRigBinder::Tags() const
{
    return WorldRef.TryGetResource<GameplayTagRegistry>();
}

void AnimRigBinder::Report(AnimDiagnosticSeverity severity, std::string code, std::string asset,
                           std::string field, std::string message)
{
    for (const AnimDiagnostic& existing : Out.Diagnostics)
    {
        // One content problem compiled into several places (a delegated
        // selector's rule under two parents) is still one problem.
        if (existing.Code == code && existing.AssetPath == asset && existing.FieldPath == field)
            return;
    }
    Out.Diagnostics.push_back(AnimDiagnostic{ severity, std::move(code), std::move(asset),
                                              std::move(field), std::move(message) });
}

std::optional<GameplayTagId> AnimRigBinder::ResolveTag(const std::string& name, const std::string& asset,
                                                       const std::string& field, std::string_view code)
{
    const GameplayTagRegistry* tags = Tags();
    const GameplayTagId id = tags != nullptr ? tags->FindTag(name) : GameplayTagId{};
    if (!id.IsValid())
    {
        // Never registered on the author's behalf: a misspelt name that became
        // a new tag would bind fine and match nothing.
        Error(std::string(code), asset, field,
              std::format("'{}' is not a gameplay tag this World declares.", name));
        return std::nullopt;
    }
    return id;
}

// The chain from the most basic schema to the rig's own, so base slots
// take the first indices.
void AnimRigBinder::BindFacts(const AnimRigData& rig)
{
    std::vector<std::pair<std::string, const AnimFactSchema*>> chain;
    std::string path = rig.FactSchemaPath;
    std::string referrer = Out.RigPath;
    std::string field = "$.data.facts";
    while (!path.empty())
    {
        if (chain.size() == kMaxSchemaChain
            || std::any_of(chain.begin(), chain.end(),
                           [&](const auto& link) { return link.first == path; }))
        {
            Error("anim.fact.extends_cycle", referrer, field,
                  std::format("The schema chain through '{}' loops or is deeper than {}.",
                              path, kMaxSchemaChain));
            return;
        }
        const AnimFactSchema* schema =
            Load<AnimFactSchema>(path, kAnimFactSchemaType, referrer, field);
        if (schema == nullptr)
            return;
        chain.emplace_back(path, schema);
        referrer = path;
        field = "$.data.extends";
        path = schema->Extends;
    }
    std::reverse(chain.begin(), chain.end());

    // Every declared slot first, across the whole chain, then every
    // derived fact: a derivation may read a slot from anywhere in the
    // chain, and a derived fact declared earlier.
    for (const auto& [schemaPath, schema] : chain)
    {
        for (std::size_t i = 0; i < schema->Slots.size(); ++i)
        {
            const AnimFactSlotDecl& slot = schema->Slots[i];
            if (Out.FindSlot(slot.Name) >= 0)
            {
                Error("anim.fact.duplicate_slot", schemaPath,
                      std::format("$.data.slots[{}].name", i),
                      std::format("'{}' is already declared by an earlier schema in the "
                                  "chain; a schema extends by adding names, never by "
                                  "redeclaring one.",
                                  slot.Name));
                continue;
            }
            AnimBoundFactSlot bound;
            bound.Name = slot.Name;
            bound.Kind = slot.Kind;
            bound.Local = slot.Local;
            bound.DeclaredIn = schemaPath;
            bound.DeclaredAt = std::format("$.data.slots[{}]", i);
            Out.Slots.push_back(std::move(bound));
        }
    }
    for (const auto& [schemaPath, schema] : chain)
    {
        for (std::size_t i = 0; i < schema->Derived.size(); ++i)
            BindDerivation(schema->Derived[i], schemaPath, i);
    }

    const std::size_t capacity = AnimFactSlotCount(Out.Capacity);
    if (Out.Slots.size() > capacity)
        Error("anim.fact.capacity", Out.RigPath, "$.data.fact_capacity",
              std::format("The schema chain declares {} facts and this rig's storage "
                          "holds {}; use Large storage or publish fewer facts.",
                          Out.Slots.size(), capacity));
    if (Out.Derivations.size() > kAnimMaxDerivations)
        Error("anim.fact.derivation_capacity", Out.RigPath, "$.data.facts",
              std::format("The schema chain declares {} derived facts, and at most {} are "
                          "allowed; derive less and publish more.",
                          Out.Derivations.size(), kAnimMaxDerivations));
}

void AnimRigBinder::BindDerivation(const AnimDerivedFactDecl& decl, const std::string& schemaPath,
                    std::size_t index)
{
    const std::string at = std::format("$.data.derived[{}]", index);
    if (Out.FindSlot(decl.Name) >= 0)
    {
        Error("anim.fact.duplicate_slot", schemaPath, at + ".name",
              std::format("'{}' is already a fact in this chain.", decl.Name));
        return;
    }

    AnimBoundDerivation derivation;
    derivation.Op = decl.Op;
    derivation.Rising = decl.Rising;
    derivation.MatchValue = decl.MatchValue;
    derivation.Compare = decl.Compare;
    derivation.WindowMs = decl.WindowMs;
    derivation.Enter = decl.Enter;
    derivation.Exit = decl.Exit;
    derivation.Constant = decl.Constant;

    if (decl.Sources.size() > kAnimMaxDerivationSources)
    {
        Error("anim.fact.too_many_sources", schemaPath, at + ".sources",
              std::format("A derivation reads at most {} facts.", kAnimMaxDerivationSources));
        return;
    }
    const bool wantsBool = decl.Op == AnimDerivationOp::Edge
        || decl.Op == AnimDerivationOp::TimeSince || decl.Op == AnimDerivationOp::MinDuration
        || decl.Op == AnimDerivationOp::And || decl.Op == AnimDerivationOp::Or
        || decl.Op == AnimDerivationOp::Not;
    for (std::size_t s = 0; s < decl.Sources.size(); ++s)
    {
        const AnimFactOperand& operand = decl.Sources[s];
        const std::string operandPath = decl.Sources.size() == 1
            ? at + ".source.fact"
            : std::format("{}.sources[{}].fact", at, s);
        const int slot = Out.FindSlot(operand.Fact);
        if (slot < 0)
        {
            // Derived facts are bound in declaration order, so a later
            // one is not found here: forward references cannot form a
            // cycle, and a name not declared at all is simply unknown.
            Error("anim.fact.unknown_operand", schemaPath, operandPath,
                  std::format("'{}' is not a slot or an earlier derived fact in this "
                              "chain.",
                              operand.Fact));
            return;
        }
        const AnimFactKind kind = Out.Slots[static_cast<std::size_t>(slot)].Kind;
        if (wantsBool && kind != AnimFactKind::Bool)
        {
            Error("anim.fact.operand_kind", schemaPath, operandPath,
                  std::format("{} reads a bool, and '{}' is a {}.",
                              AnimDerivationOpName(decl.Op), operand.Fact,
                              AnimFactKindName(kind)));
            return;
        }
        if (!wantsBool && kind != AnimFactKind::Float && kind != AnimFactKind::Int)
        {
            Error("anim.fact.operand_kind", schemaPath, operandPath,
                  std::format("{} reads a number, and '{}' is a {}.",
                              AnimDerivationOpName(decl.Op), operand.Fact,
                              AnimFactKindName(kind)));
            return;
        }
        if (operand.Negate && kind != AnimFactKind::Bool)
        {
            Error("anim.fact.operand_kind", schemaPath, operandPath,
                  "Only a bool operand can be negated.");
            return;
        }
        if (s == 0)
            derivation.SourceKind = kind;
        derivation.Sources[s] = AnimBoundOperand{ static_cast<std::uint8_t>(slot),
                                                  operand.Negate };
    }
    derivation.SourceCount = static_cast<std::uint8_t>(decl.Sources.size());

    AnimBoundFactSlot result;
    result.Name = decl.Name;
    result.Kind = decl.ResultKind();
    // A fact derived from a local fact is local: it inherits the
    // restriction on what it may influence.
    for (std::size_t s = 0; s < derivation.SourceCount; ++s)
        result.Local = result.Local || Out.Slots[derivation.Sources[s].Slot].Local;
    result.Derivation = static_cast<int>(Out.Derivations.size());
    result.DeclaredIn = schemaPath;
    result.DeclaredAt = at;
    derivation.Result = static_cast<std::uint8_t>(Out.Slots.size());
    Out.Slots.push_back(std::move(result));

    if (decl.IsTemporal())
    {
        Out.HasTemporalDerivations = true;
        Out.HorizonMs = std::max(Out.HorizonMs, decl.WindowMs);
    }
    Out.Derivations.push_back(derivation);
}

void AnimRigBinder::BindProviders()
{
    const AnimFactProviders* providers = WorldRef.TryGetResource<AnimFactProviders>();
    for (AnimBoundFactSlot& slot : Out.Slots)
    {
        if (slot.Derivation >= 0 || slot.Kind == AnimFactKind::TagSet || providers == nullptr)
            continue;
        slot.Provider = providers->IndexOf(slot.Name);
        if (slot.Provider < 0)
            continue;
        const AnimFactKind provided = providers->At(slot.Provider).Kind;
        if (provided != slot.Kind)
        {
            Error("anim.fact.provider_kind", slot.DeclaredIn, slot.DeclaredAt + ".kind",
                  std::format("'{}' is declared {} and its provider publishes {}.",
                              slot.Name, AnimFactKindName(slot.Kind),
                              AnimFactKindName(provided)));
            slot.Provider = -1;
        }
    }
}

void AnimRigBinder::BindRequests(const AnimRigData& rig)
{
    if (rig.RequestSchemaPath.empty())
        return;
    const AnimRequestSchema* schema = Load<AnimRequestSchema>(
        rig.RequestSchemaPath, kAnimRequestSchemaType, Out.RigPath, "$.data.requests");
    if (schema == nullptr)
        return;
    Out.HasRequestSchema = true;
    const GameplayTagRegistry* tags = WorldRef.TryGetResource<GameplayTagRegistry>();
    for (std::size_t i = 0; i < schema->Intents.size(); ++i)
    {
        const AnimRequestIntentDeclaration& declared = schema->Intents[i];
        AnimBoundIntent intent;
        intent.Name = declared.Intent;
        intent.Intent = tags != nullptr ? tags->FindTag(declared.Intent) : GameplayTagId{};
        if (!intent.Intent.IsValid())
        {
            // Never registered on the author's behalf: a misspelt intent
            // that became a new tag would bind fine and be requested by
            // nothing.
            Error("anim.request.intent_unresolved", rig.RequestSchemaPath,
                  std::format("$.data.intents[{}].intent", i),
                  std::format("'{}' is not a gameplay tag this World declares.",
                              declared.Intent));
            continue;
        }
        for (std::uint8_t p = 0; p < declared.ParamCount; ++p)
            intent.Params.push_back({ declared.Params[p].Name, declared.Params[p].Kind });
        Out.Intents.push_back(std::move(intent));
    }
}

void AnimRigBinder::BindLayers(const AnimRigData& rig)
{
    const GameplayTagRegistry* tags = WorldRef.TryGetResource<GameplayTagRegistry>();
    for (std::size_t i = 0; i < rig.Layers.size(); ++i)
    {
        AnimBoundLayer layer;
        layer.NameText = rig.Layers[i].Name;
        layer.Name = tags != nullptr ? tags->FindTag(layer.NameText) : GameplayTagId{};
        layer.Mode = rig.Layers[i].Mode;
        layer.Weight = rig.Layers[i].Weight;
        if (!rig.Layers[i].Idle.empty())
        {
            if (std::optional<GameplayTagId> idle = ResolveTag(
                    rig.Layers[i].Idle, Out.RigPath, std::format("$.data.layers[{}].idle", i),
                    "anim.rig.idle_unresolved"))
                layer.Idle = *idle;
        }
        if (!layer.Name.IsValid())
            Error("anim.rig.layer_unresolved", Out.RigPath,
                  std::format("$.data.layers[{}].name", i),
                  std::format("'{}' is not a gameplay tag this World declares.",
                              layer.NameText));
        Out.Layers.push_back(std::move(layer));
    }
}

int AnimBoundRig::FindSlot(std::string_view name) const
{
    for (std::size_t i = 0; i < Slots.size(); ++i)
    {
        if (Slots[i].Name == name)
            return static_cast<int>(i);
    }
    return -1;
}

const AnimBoundIntent* AnimBoundRig::FindIntent(GameplayTagId intent) const
{
    for (const AnimBoundIntent& bound : Intents)
    {
        if (bound.Intent == intent)
            return &bound;
    }
    return nullptr;
}

int AnimBoundRig::FindBehaviorIndex(GameplayTagId behavior) const
{
    for (std::size_t i = 0; i < Behaviors.size(); ++i)
    {
        if (Behaviors[i].Tag == behavior)
            return static_cast<int>(i);
    }
    return -1;
}

const AnimBoundBehavior* AnimBoundRig::FindBehavior(GameplayTagId behavior) const
{
    const int index = FindBehaviorIndex(behavior);
    return index >= 0 ? &Behaviors[static_cast<std::size_t>(index)] : nullptr;
}

namespace
{
    AnimBoundRig Bind(const DataAssetCache& data, const AnimationClipCache* clips,
                      DataAssetHandle handle, const World& world,
                      std::vector<std::pair<DataAssetHandle, std::uint64_t>>* versions)
    {
        AnimBoundRig bound;
        bound.RigPath = std::string(data.GetName(handle));
        const AnimRigData* rig = data.TryGet<AnimRigData>(handle, kAnimRigType);
        if (rig == nullptr)
        {
            bound.Diagnostics.push_back(AnimDiagnostic{
                AnimDiagnosticSeverity::Error, "anim.asset.wrong_subtype", bound.RigPath, {},
                std::format("'{}' is not an {}.", bound.RigPath, kAnimRigType) });
            return bound;
        }
        if (versions != nullptr)
            versions->push_back({ handle, data.GetReloadVersion(handle) });

        AnimRigBinder binder{ data, clips, world, bound, versions };
        bound.Capacity = rig->FactCapacity;
        bound.HasFacts = rig->HasFacts();
        if (bound.HasFacts)
        {
            binder.BindFacts(*rig);
            binder.BindProviders();
        }
        binder.BindRequests(*rig);
        binder.BindLayers(*rig);
        // Behaviors before selectors and slot maps, which name them.
        binder.BindBehaviors(*rig);
        binder.BindSelectors(*rig);
        binder.BindSlotMaps(*rig);
        bound.Valid = !HasAnimErrors(bound.Diagnostics);
        return bound;
    }
}

AnimBoundRig BindAnimRig(const DataAssetCache& data, const AnimationClipCache* clips,
                         DataAssetHandle rig, const World& world)
{
    return Bind(data, clips, rig, world, nullptr);
}

bool AnimRigBindings::IsCurrent(const Entry& entry, const World& world) const
{
    for (const auto& [handle, version] : entry.Versions)
    {
        if (Data->GetReloadVersion(handle) != version)
            return false;
    }
    const GameplayTagRegistry* tags = world.TryGetResource<GameplayTagRegistry>();
    const AnimFactProviders* providers = world.TryGetResource<AnimFactProviders>();
    return entry.TagCount == (tags != nullptr ? tags->Size() : 0)
        && entry.ProviderRevision == (providers != nullptr ? providers->Revision() : 0);
}

const AnimBoundRig* AnimRigBindings::Resolve(DataAssetHandle rig, const World& world)
{
    if (Data == nullptr || !rig.IsValid() || Data->GetSubtype(rig) != kAnimRigType)
        return nullptr;

    Entry& entry = Entries[rig.ToToken()];
    if (entry.Bound.Generation != 0 && IsCurrent(entry, world))
        return &entry.Bound;

    entry.Versions.clear();
    entry.Bound = Bind(*Data, Clips, rig, world, &entry.Versions);
    // Unique across every bindings instance: a republished resource starts
    // empty, and per-entity history must not mistake its first generation for
    // one it kept memory against.
    static std::atomic<std::uint64_t> generations{ 0 };
    entry.Bound.Generation = ++generations;
    const GameplayTagRegistry* tags = world.TryGetResource<GameplayTagRegistry>();
    const AnimFactProviders* providers = world.TryGetResource<AnimFactProviders>();
    entry.TagCount = tags != nullptr ? tags->Size() : 0;
    entry.ProviderRevision = providers != nullptr ? providers->Revision() : 0;
    ++Rebuilds;
    return &entry.Bound;
}
