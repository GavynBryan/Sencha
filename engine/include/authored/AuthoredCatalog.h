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

// One World's catalog of one kind of authored contract -- verbs, queries or
// events. Metadata only; slot, revision and registration rules are described
// in docs/gameplay/authored-api.md.

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

// Process-unique, so a catalog that reuses a destroyed one's address is still a
// different catalog.
[[nodiscard]] std::uint64_t NextAuthoredCatalogNumber();

template<AuthoredCatalogTraits Traits>
class AuthoredCatalog;

template<AuthoredCatalogTraits Traits>
class AuthoredRegistrationScope;

// Publishes every scope's batch into its catalog, or none of them. Each scope
// commits once; refused batches leave their errors on their own catalog's
// InstallationErrors.
template<AuthoredCatalogTraits... Traits>
[[nodiscard]] bool CommitTogether(AuthoredRegistrationScope<Traits>&... scopes);

// One provider's batch. Destroyed without a commit, it publishes nothing.
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

    // False means the batch will fail. Declaring continues, so every problem is
    // reported in one run.
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

    [[nodiscard]] bool Commit() { return CommitTogether(*this); }

    [[nodiscard]] bool HasErrors() const { return !Errors_.empty(); }
    [[nodiscard]] std::span<const std::string> Errors() const { return Errors_; }
    [[nodiscard]] std::string_view Provider() const { return Provider_; }

private:
    template<AuthoredCatalogTraits... Others>
    friend bool CommitTogether(AuthoredRegistrationScope<Others>&... scopes);

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
    void Refuse() { Catalog_.RecordInstallationErrors(Errors_); }

    AuthoredCatalog<Traits>& Catalog_;
    std::string Provider_;
    std::vector<Definition> Pending;
    std::vector<std::string> Errors_;
    bool Committed = false;
};

template<AuthoredCatalogTraits... Traits>
bool CommitTogether(AuthoredRegistrationScope<Traits>&... scopes)
{
    // Not short-circuited: every scope reports all of its problems.
    const bool begun = (static_cast<int>(scopes.BeginCommit()) & ...) != 0;
    if (!begun)
        return false;
    const bool ready = (static_cast<int>(scopes.Prepare()) & ...) != 0;
    if (!ready)
    {
        (scopes.Refuse(), ...);
        return false;
    }
    (scopes.Publish(), ...);
    return true;
}

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

    [[nodiscard]] CatalogId Catalog() const { return Catalog_; }

    [[nodiscard]] Id Find(std::string_view name) const
    {
        const auto it = IdsByName.find(std::string(name));
        if (it == IdsByName.end())
            return {};
        return Slots[IndexOf(it->second)].State == SlotState::Live ? it->second : Id{};
    }

    // Invalid when nothing live carries the name.
    [[nodiscard]] Handle Resolve(std::string_view name) const
    {
        const Id id = Find(name);
        if (!id.IsValid())
            return {};
        return Handle{ .Catalog = Catalog_, .Slot = id, .Contract = Revision(id) };
    }

    [[nodiscard]] bool IsCurrent(const Handle& handle) const
    {
        return handle.Catalog == Catalog_ && IsLive(handle.Slot)
            && Revision(handle.Slot) == handle.Contract;
    }

    // A bare id carries no catalog: one minted elsewhere is indistinguishable
    // here. Anything stored across frames is a Handle.
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

    // Moves when the contract changes and when a retired name is revived.
    [[nodiscard]] ContractRevision Revision(Id id) const
    {
        if (!IsLive(id))
            return {};
        return Slots[IndexOf(id)].Revision;
    }

    [[nodiscard]] std::string_view Provider(Id id) const
    {
        if (!IsLive(id))
            return {};
        return Slots[IndexOf(id)].Provider;
    }

    // Slots are kept, so an id cached against an unloaded provider resolves to
    // nothing rather than to whatever is declared next.
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

    [[nodiscard]] std::size_t SlotCount() const { return Slots.size(); }

    // Moves on every publish and every retirement.
    [[nodiscard]] std::uint64_t Generation() const { return Generation_; }

    // In id order.
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

    // Every refused batch since the last clear. The vocabulary hook returns
    // void, so this is where its host learns what failed.
    [[nodiscard]] std::span<const std::string> InstallationErrors() const
    {
        return InstallationErrors_;
    }
    void ClearInstallationErrors() { InstallationErrors_.clear(); }

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
            if (slot.State == SlotState::Live && slot.Provider != provider)
            {
                errors.push_back(std::format(
                    "'{}' is already declared by provider '{}'; provider '{}' cannot redeclare it",
                    definition.Name, slot.Provider, provider));
            }
        }
        return errors.size() == before;
    }

    // Cannot fail once ValidatePublish accepted the batch, which is what lets
    // CommitTogether publish into several catalogs after all of them agreed.
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
            const bool revived = slot.State == SlotState::Retired;
            if (revived || !Traits::ContractsMatch(slot.Declared, definition))
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
    // Retired names included, so a revived name gets its old slot back.
    std::unordered_map<std::string, Id> IdsByName;
};
