#pragma once

#include <authored/VerbId.h>
#include <core/metadata/DataSchema.h>

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

//=============================================================================
// VerbRegistry
//
// One World's authored vocabulary: which semantic operations content may name,
// and what each one's arguments are. A World resource, because the names
// content resolves against are a property of the entity universe it was loaded
// into -- two editor documents are two catalogs, and the same number means a
// different verb in each.
//
// Metadata only. What a verb *does* is a registered implementation held by the
// dispatcher, which is a separate object with a separate lifetime: an editor
// installs this to offer and validate a vocabulary without acquiring the power
// to run any of it.
//
// Ids follow InputActionRegistry's identity contract for the same reason it
// has one: a name keeps its slot for the catalog's lifetime, a name the
// vocabulary stops declaring retires its slot rather than freeing it for
// reuse, and a name that comes back revives the slot it had. A cached id that
// resolved to nothing is the truth; a cached id that silently resolved to
// whichever verb was declared next is the failure that reads as the wrong
// thing happening.
//=============================================================================

// An argument root with no arguments in it. Most verbs take none, and a
// declaration that forgot to say "record" is a mistake with no upside.
[[nodiscard]] inline DataFieldSchema EmptyVerbArguments()
{
    DataFieldSchema root;
    root.Kind = DataFieldKind::Record;
    return root;
}

// What a verb is called, what it takes, and how an authoring surface should
// say it. The name is the persisted contract; everything else but the argument
// schema is presentation.
struct VerbDefinition
{
    // Exact, case-sensitive, dot-separated: "runtime.resume". A prefix
    // organizes a picker and is never parsed to select a subsystem.
    std::string Name;

    std::string DisplayName;
    std::string Description;

    // Optional presentation grouping. Not an identity and not a namespace.
    std::string Category;

    // A record root, one child per named argument. Reusing DataFieldSchema is
    // what keeps ranges, enum choices, optionality and nesting described once
    // for the inspector, the binding compiler, and the .sdata validator.
    DataFieldSchema Arguments = EmptyVerbArguments();
};

// Whether a name is spelled the way the catalog persists names. Dot-separated
// ASCII segments, each starting with a letter or underscore and continuing
// with letters, digits or underscores. No empty segment, no surrounding
// whitespace, and no normalization: a name is stored exactly as declared.
[[nodiscard]] bool IsValidVerbName(std::string_view name);

// Whether two declarations mean the same argument contract. Structure, keys,
// kinds, constraints, enum values, optionality and nesting -- not display
// names, summaries, descriptions, units or editor hints, which a provider may
// reword without invalidating a binding compiled last week.
[[nodiscard]] bool VerbContractsMatch(const DataFieldSchema& left,
                                      const DataFieldSchema& right);

class VerbRegistry;

//-----------------------------------------------------------------------------
// VerbRegistrationScope
//
// One provider's batch, validated before any of it is published.
//
// The game hook that declares a module's vocabulary returns void, so a module
// that ignores a failed Declare cannot be relied on to stop. The scope carries
// the errors instead and the host reads them after the hook returns, which is
// what makes installation checked without adding a virtual method or changing
// a signature a shipped module compiled against.
//
// A scope that is destroyed without Commit publishes nothing.
//-----------------------------------------------------------------------------
class VerbRegistrationScope
{
public:
    VerbRegistrationScope(VerbRegistry& registry, std::string provider);

    VerbRegistrationScope(const VerbRegistrationScope&) = delete;
    VerbRegistrationScope& operator=(const VerbRegistrationScope&) = delete;
    VerbRegistrationScope(VerbRegistrationScope&&) = delete;
    VerbRegistrationScope& operator=(VerbRegistrationScope&&) = delete;

    // Records one declaration. False means this batch will fail; the caller may
    // keep declaring so an author sees every problem at once rather than one
    // per run.
    bool Declare(VerbDefinition definition);

    // Publishes the whole batch, or none of it. False leaves the catalog
    // exactly as it was.
    [[nodiscard]] bool Commit();

    [[nodiscard]] bool HasErrors() const { return !Errors_.empty(); }
    [[nodiscard]] std::span<const std::string> Errors() const { return Errors_; }

private:
    VerbRegistry& Registry;
    std::string Provider;
    std::vector<VerbDefinition> Pending;
    std::vector<std::string> Errors_;
    bool Committed = false;
};

class VerbRegistry
{
public:
    VerbRegistry();

    VerbRegistry(const VerbRegistry&) = delete;
    VerbRegistry& operator=(const VerbRegistry&) = delete;
    VerbRegistry(VerbRegistry&&) = delete;
    VerbRegistry& operator=(VerbRegistry&&) = delete;

    // This catalog's identity, minted once and never reused. A compiled binding
    // records it so a binding resolved against a destroyed World cannot be
    // invoked against whichever registry took its address.
    [[nodiscard]] VerbCatalogId Catalog() const { return Catalog_; }

    [[nodiscard]] VerbId Find(std::string_view name) const;

    // Null for a retired id, an id from another catalog, or one this registry
    // never minted.
    [[nodiscard]] const VerbDefinition* Get(VerbId id) const;
    [[nodiscard]] bool IsLive(VerbId id) const;

    // Zero for anything Get would return null for. Moves when a live name's
    // argument contract changes, and again when a retired name comes back --
    // a revived slot is never assumed to mean what it meant before.
    [[nodiscard]] VerbContractRevision Revision(VerbId id) const;

    // Which provider declared the verb currently in the slot. Empty for a
    // retired one. Diagnostics and conflict reporting; never dispatch input.
    [[nodiscard]] std::string_view Provider(VerbId id) const;

    // Retires every name the named provider currently owns. Slots stay, so an
    // id cached against an unloaded module resolves to nothing rather than to
    // whatever is declared next.
    void RetireProvider(std::string_view provider);

    // Slots ever minted, retired ones included.
    [[nodiscard]] std::size_t SlotCount() const { return Slots.size(); }

    // Moves on every published batch and every retirement. What a compiled
    // set compares to know whether a name that failed to resolve might resolve
    // now, or one that did might have moved.
    [[nodiscard]] std::uint64_t Generation() const { return Generation_; }

    // Every live verb, ordered by id. Deterministic by construction: no tool,
    // diagnostic, fixture or test ever sees hash order.
    [[nodiscard]] std::vector<VerbId> LiveVerbs() const;

    // Why a batch was refused, from every scope that has failed against this
    // catalog since the last clear.
    //
    // This is what makes installation checked through a hook that returns void.
    // A provider is free to ignore what Commit told it; the host reads this
    // afterwards and declines to start, and the diagnostics say which verb and
    // which provider rather than "vocabulary installation failed".
    [[nodiscard]] std::span<const std::string> InstallationErrors() const
    {
        return InstallationErrors_;
    }
    void ClearInstallationErrors() { InstallationErrors_.clear(); }

    [[nodiscard]] static std::size_t IndexOf(VerbId id) { return id.Value - 1; }

private:
    friend class VerbRegistrationScope;

    enum class SlotState : std::uint8_t
    {
        Live,
        Retired,
    };

    struct Slot
    {
        VerbDefinition Definition;
        std::string Provider;
        VerbContractRevision Revision{ 1 };
        SlotState State = SlotState::Live;
    };

    // Applies a validated batch. Callers reach this through a scope, which is
    // what guarantees the batch was checked as a whole first.
    bool Publish(std::string_view provider,
                 std::vector<VerbDefinition>& definitions,
                 std::vector<std::string>& errors);

    VerbCatalogId Catalog_;
    std::vector<Slot> Slots;
    std::vector<std::string> InstallationErrors_;
    std::uint64_t Generation_ = 0;
    // Every name ever declared, retired ones included, so a name that comes
    // back gets the id it had before rather than a second slot.
    std::unordered_map<std::string, VerbId> IdsByName;
};
