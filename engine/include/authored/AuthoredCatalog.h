#pragma once

#include <authored/AuthoredHandle.h>
#include <authored/AuthoredSchema.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

//=============================================================================
// AuthoredCatalog
//
// One World's vocabulary of one kind of authored contract -- verbs, queries or
// events: which names content may persist, and what each one's shape is.
//
// A World resource, because the names content resolves against are a property
// of the entity universe it was loaded into -- two editor documents are two
// catalogs, and the same number means a different entry in each. Metadata
// only: what an entry does is held by that kind's dispatcher, a separate
// object with a separate lifetime, so an editor can install a catalog to offer
// and validate a vocabulary without acquiring the power to run any of it.
//
// Ids follow InputActionRegistry's identity contract for the same reason it
// has one: a name keeps its slot for the catalog's lifetime, a name the
// vocabulary stops declaring retires its slot rather than freeing it for
// reuse, and a name that comes back revives the slot it had. A cached id that
// resolved to nothing is the truth; a cached id that silently resolved to
// whichever entry was declared next is the failure that reads as the wrong
// thing happening.
//
// The kinds differ only in what a definition holds, how one is validated, and
// what counts as a changed contract. A traits type supplies those three things;
// everything about slots, providers, revisions and installation is here once.
//=============================================================================

template<typename T>
concept AuthoredCatalogTraits = requires(const typename T::Definition& definition,
                                         std::vector<std::string>& errors) {
    typename T::Id;
    typename T::Revision;
    typename T::CatalogId;
    { T::Noun } -> std::convertible_to<std::string_view>;
    { definition.Name } -> std::convertible_to<std::string_view>;
    T::Validate(definition, errors);
    { T::ContractsMatch(definition, definition) } -> std::same_as<bool>;
};

// Every catalog ever made in this process gets its own number, whatever it
// catalogs, so a compiled binding checked against a registry that reused a
// destroyed one's address fails the check rather than passing it.
[[nodiscard]] std::uint64_t NextAuthoredCatalogNumber();

template<AuthoredCatalogTraits Traits>
class AuthoredCatalog;

//-----------------------------------------------------------------------------
// AuthoredRegistrationScope
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
template<AuthoredCatalogTraits Traits>
class AuthoredRegistrationScope
{
public:
    using Definition = typename Traits::Definition;

    AuthoredRegistrationScope(AuthoredCatalog<Traits>& catalog, std::string provider)
        : Catalog_(catalog)
        , Provider_(std::move(provider))
    {
        if (Provider_.empty())
            Errors_.push_back(std::format("a {} registration scope needs a provider name", Traits::Noun));
    }

    AuthoredRegistrationScope(const AuthoredRegistrationScope&) = delete;
    AuthoredRegistrationScope& operator=(const AuthoredRegistrationScope&) = delete;
    AuthoredRegistrationScope(AuthoredRegistrationScope&&) = delete;
    AuthoredRegistrationScope& operator=(AuthoredRegistrationScope&&) = delete;

    // Records one declaration. False means this batch will fail; the caller may
    // keep declaring so an author sees every problem at once rather than one
    // per run.
    bool Declare(Definition definition)
    {
        const std::size_t before = Errors_.size();

        if (!IsValidAuthoredName(definition.Name))
        {
            Errors_.push_back(std::format(
                "'{}' is not a {} name: dot-separated identifier segments, no empty segment",
                definition.Name, Traits::Noun));
        }
        for (const Definition& pending : Pending)
        {
            if (pending.Name == definition.Name)
            {
                Errors_.push_back(std::format("'{}' is declared twice by provider '{}'",
                                              definition.Name, Provider_));
                break;
            }
        }
        Traits::Validate(definition, Errors_);

        if (Errors_.size() != before)
            return false;

        Pending.push_back(std::move(definition));
        return true;
    }

    // Publishes the whole batch, or none of it. False leaves the catalog
    // exactly as it was.
    [[nodiscard]] bool Commit()
    {
        if (!BeginCommit())
            return false;
        if (!Prepare())
        {
            Refuse();
            return false;
        }
        Publish();
        return true;
    }

    [[nodiscard]] bool HasErrors() const { return !Errors_.empty(); }
    [[nodiscard]] std::span<const std::string> Errors() const { return Errors_; }
    [[nodiscard]] std::string_view Provider() const { return Provider_; }

    // The three steps Commit takes, public so a caller committing to several
    // catalogs at once can check every batch before publishing any of them.
    // BeginCommit is false for a scope that has already committed; Prepare
    // checks the batch against the live catalog without changing it; Publish
    // applies a prepared batch and cannot fail; Refuse leaves the batch's
    // errors where the host reads them.
    [[nodiscard]] bool BeginCommit()
    {
        if (Committed)
        {
            Errors_.push_back(std::format("a {} registration scope commits once", Traits::Noun));
            return false;
        }
        Committed = true;
        return true;
    }

    [[nodiscard]] bool Prepare()
    {
        return Errors_.empty() && Catalog_.ValidatePublish(Provider_, Pending, Errors_);
    }

    void Publish() { Catalog_.ApplyPublish(Provider_, Pending); }

    void Refuse()
    {
        // Left where the host will find them. A scope commits once, so this
        // cannot double-report, and a provider that ignores Commit's return
        // value has still made the failure visible to whoever owns startup.
        Catalog_.RecordInstallationErrors(Errors_);
    }

private:
    AuthoredCatalog<Traits>& Catalog_;
    std::string Provider_;
    std::vector<Definition> Pending;
    std::vector<std::string> Errors_;
    bool Committed = false;
};

template<AuthoredCatalogTraits Traits>
class AuthoredCatalog
{
public:
    using Definition = typename Traits::Definition;
    using Id = typename Traits::Id;
    using ContractRevision = typename Traits::Revision;
    using CatalogId = typename Traits::CatalogId;
    using Handle = AuthoredHandle<CatalogId, Id, ContractRevision>;

    AuthoredCatalog()
        : Catalog_{ NextAuthoredCatalogNumber() }
    {
    }

    AuthoredCatalog(const AuthoredCatalog&) = delete;
    AuthoredCatalog& operator=(const AuthoredCatalog&) = delete;
    AuthoredCatalog(AuthoredCatalog&&) = delete;
    AuthoredCatalog& operator=(AuthoredCatalog&&) = delete;

    // This catalog's identity, minted once and never reused. A compiled binding
    // records it so a binding resolved against a destroyed World cannot be
    // invoked against whichever registry took its address.
    [[nodiscard]] CatalogId Catalog() const { return Catalog_; }

    [[nodiscard]] Id Find(std::string_view name) const
    {
        const auto it = IdsByName.find(std::string(name));
        if (it == IdsByName.end())
            return {};
        return Slots[IndexOf(it->second)].State == SlotState::Live ? it->second : Id{};
    }

    // The name resolved against this catalog as it is now, for a consumer to
    // store in place of the name. Invalid when nothing live carries it.
    [[nodiscard]] Handle Resolve(std::string_view name) const
    {
        const Id id = Find(name);
        if (!id.IsValid())
            return {};
        return Handle{ .Catalog = Catalog_, .Slot = id, .Contract = Revision(id) };
    }

    // Whether a handle still means what it meant when it was resolved: minted
    // by this catalog, still live, and still the same contract.
    [[nodiscard]] bool IsCurrent(const Handle& handle) const
    {
        return handle.Catalog == Catalog_ && IsLive(handle.Slot)
            && Revision(handle.Slot) == handle.Contract;
    }

    // Null for a retired id or one this catalog never minted. A bare id
    // carries no catalog, so an id minted by another catalog is not
    // distinguishable here; a consumer that stores one stores a Handle.
    [[nodiscard]] const Definition* Get(Id id) const
    {
        if (!IsLive(id))
            return nullptr;
        return &Slots[IndexOf(id)].Declared;
    }

    [[nodiscard]] bool IsLive(Id id) const
    {
        if (!id.IsValid() || IndexOf(id) >= Slots.size())
            return false;
        return Slots[IndexOf(id)].State == SlotState::Live;
    }

    // Zero for anything Get would return null for. Moves when a live name's
    // contract changes, and again when a retired name comes back -- a revived
    // slot is never assumed to mean what it meant before.
    [[nodiscard]] ContractRevision Revision(Id id) const
    {
        if (!IsLive(id))
            return {};
        return Slots[IndexOf(id)].Revision;
    }

    // Which provider declared the entry currently in the slot. Empty for a
    // retired one. Diagnostics and conflict reporting; never dispatch input.
    [[nodiscard]] std::string_view Provider(Id id) const
    {
        if (!IsLive(id))
            return {};
        return Slots[IndexOf(id)].Provider;
    }

    // Retires every name the named provider currently owns. Slots stay, so an
    // id cached against an unloaded module resolves to nothing rather than to
    // whatever is declared next.
    void RetireProvider(std::string_view provider)
    {
        bool retired = false;
        for (Slot& slot : Slots)
        {
            if (slot.State != SlotState::Live || slot.Provider != provider)
                continue;
            slot.State = SlotState::Retired;
            slot.Provider.clear();
            retired = true;
        }
        if (retired)
            ++Generation_;
    }

    // Slots ever minted, retired ones included.
    [[nodiscard]] std::size_t SlotCount() const { return Slots.size(); }

    // Moves on every published batch and every retirement. What a compiled
    // set compares to know whether a name that failed to resolve might resolve
    // now, or one that did might have moved.
    [[nodiscard]] std::uint64_t Generation() const { return Generation_; }

    // Every live entry, ordered by id. Deterministic by construction: no tool,
    // diagnostic, fixture or test ever sees hash order.
    [[nodiscard]] std::vector<Id> Live() const
    {
        std::vector<Id> live;
        live.reserve(Slots.size());
        for (std::size_t index = 0; index < Slots.size(); ++index)
        {
            if (Slots[index].State == SlotState::Live)
                live.push_back(Id{ static_cast<IdValue>(index + 1) });
        }
        return live;
    }

    // Why a batch was refused, from every scope that has failed against this
    // catalog since the last clear.
    //
    // This is what makes installation checked through a hook that returns void.
    // A provider is free to ignore what Commit told it; the host reads this
    // afterwards and declines to start, and the diagnostics say which entry and
    // which provider rather than "vocabulary installation failed".
    [[nodiscard]] std::span<const std::string> InstallationErrors() const
    {
        return InstallationErrors_;
    }
    void ClearInstallationErrors() { InstallationErrors_.clear(); }

    // Whether `provider` may publish `definitions` against the catalog as it is
    // now. Appends one message per conflict and changes nothing, so several
    // catalogs can all be asked before any of them is written.
    [[nodiscard]] bool ValidatePublish(std::string_view provider,
                                       std::span<const Definition> definitions,
                                       std::vector<std::string>& errors) const
    {
        const std::size_t before = errors.size();
        for (const Definition& definition : definitions)
        {
            const auto it = IdsByName.find(definition.Name);
            if (it == IdsByName.end())
                continue;

            const Slot& slot = Slots[IndexOf(it->second)];
            if (slot.State == SlotState::Retired)
                continue;
            if (slot.Provider != provider)
            {
                errors.push_back(std::format(
                    "'{}' is already declared by provider '{}'; provider '{}' cannot redeclare it",
                    definition.Name, slot.Provider, provider));
            }
        }
        return errors.size() == before;
    }

    [[nodiscard]] static std::size_t IndexOf(Id id) { return id.Value - 1; }

private:
    friend class AuthoredRegistrationScope<Traits>;

    enum class SlotState : std::uint8_t
    {
        Live,
        Retired,
    };

    struct Slot
    {
        Definition Declared;
        std::string Provider;
        ContractRevision Revision{ 1 };
        SlotState State = SlotState::Live;
    };

    using IdValue = decltype(Id{}.Value);

    // Applies a batch ValidatePublish accepted. Reached only through a scope,
    // which is what guarantees the batch was validated as a whole first; it
    // cannot fail, which is what lets a caller publish into several catalogs
    // once every one of them has agreed.
    void ApplyPublish(std::string_view provider, std::vector<Definition>& definitions)
    {
        for (Definition& definition : definitions)
        {
            const auto it = IdsByName.find(definition.Name);
            if (it == IdsByName.end())
            {
                Slot slot;
                slot.Declared = std::move(definition);
                slot.Provider = std::string(provider);
                Slots.push_back(std::move(slot));
                IdsByName.emplace(Slots.back().Declared.Name,
                                  Id{ static_cast<IdValue>(Slots.size()) });
                continue;
            }

            Slot& slot = Slots[IndexOf(it->second)];
            // A revived name is never assumed to mean what it meant before, and
            // a changed contract invalidates the bindings compiled against the
            // old one. Both move the revision; rewording a label does not.
            const bool revived = slot.State == SlotState::Retired;
            const bool contractChanged = !Traits::ContractsMatch(slot.Declared, definition);
            if (revived || contractChanged)
                slot.Revision = ContractRevision{ slot.Revision.Value + 1 };

            slot.Declared = std::move(definition);
            slot.Provider = std::string(provider);
            slot.State = SlotState::Live;
        }
        ++Generation_;
    }

    void RecordInstallationErrors(std::span<const std::string> errors)
    {
        InstallationErrors_.insert(InstallationErrors_.end(), errors.begin(), errors.end());
    }

    CatalogId Catalog_;
    std::vector<Slot> Slots;
    std::vector<std::string> InstallationErrors_;
    std::uint64_t Generation_ = 0;
    // Every name ever declared, retired ones included, so a name that comes
    // back gets the id it had before rather than a second slot.
    std::unordered_map<std::string, Id> IdsByName;
};
