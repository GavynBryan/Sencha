#include <authored/VerbRegistry.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <format>
#include <utility>

namespace
{
    // Every catalog ever made in this process gets its own number, so a
    // compiled binding checked against a registry that reused a destroyed
    // one's address fails the check rather than passing it.
    std::uint64_t NextCatalogNumber()
    {
        static std::atomic<std::uint64_t> counter{ 0 };
        return counter.fetch_add(1, std::memory_order_relaxed) + 1;
    }

    [[nodiscard]] bool IsNameStart(char c)
    {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    }

    [[nodiscard]] bool IsNameBody(char c)
    {
        return IsNameStart(c) || (c >= '0' && c <= '9');
    }

    [[nodiscard]] bool DefaultMatchesKind(const DataFieldSchema& field)
    {
        if (std::holds_alternative<std::monostate>(field.Default))
            return true;

        switch (field.Kind)
        {
        case DataFieldKind::Bool:
            return std::holds_alternative<bool>(field.Default);
        case DataFieldKind::Int:
            return std::holds_alternative<int64_t>(field.Default);
        case DataFieldKind::Float:
            return std::holds_alternative<double>(field.Default)
                || std::holds_alternative<int64_t>(field.Default);
        case DataFieldKind::String:
        case DataFieldKind::Enum:
        case DataFieldKind::AssetRef:
        case DataFieldKind::DataAssetRef:
        case DataFieldKind::GameplayTag:
        case DataFieldKind::Entity:
            return std::holds_alternative<std::string>(field.Default);
        case DataFieldKind::Optional:
            // An optional's default is a value for the thing it wraps: "absent
            // unless nobody said otherwise" is a real and common contract, and
            // the compiler unwraps it the same way.
            return field.Children.size() == 1 && DefaultMatchesKind(field.Children.front());
        case DataFieldKind::Vector:
        case DataFieldKind::Record:
        case DataFieldKind::Array:
            // A default for these would be a literal DataDefaultValue has no
            // alternative for, so a declaration carrying one is a mistake worth
            // reporting rather than a value to ignore.
            return false;
        }
        return false;
    }

    void ValidateField(const DataFieldSchema& field,
                       const std::string& path,
                       std::vector<std::string>& errors)
    {
        if (!DefaultMatchesKind(field))
            errors.push_back(std::format("{}: default value does not match the field kind", path));

        if (field.Numeric.Minimum && field.Numeric.Maximum
            && *field.Numeric.Minimum > *field.Numeric.Maximum)
        {
            errors.push_back(std::format("{}: minimum is above maximum", path));
        }

        switch (field.Kind)
        {
        case DataFieldKind::Record:
        {
            std::vector<std::string_view> seen;
            seen.reserve(field.Children.size());
            for (const DataFieldSchema& child : field.Children)
            {
                if (child.Key.empty())
                {
                    errors.push_back(std::format("{}: a record member needs a key", path));
                    continue;
                }
                if (std::ranges::find(seen, std::string_view(child.Key)) != seen.end())
                {
                    errors.push_back(
                        std::format("{}: duplicate member key '{}'", path, child.Key));
                    continue;
                }
                seen.push_back(child.Key);
                ValidateField(child, std::format("{}.{}", path, child.Key), errors);
            }
            break;
        }
        case DataFieldKind::Array:
        case DataFieldKind::Optional:
            if (field.Children.size() != 1)
            {
                errors.push_back(std::format(
                    "{}: an array or optional describes exactly one element", path));
                break;
            }
            ValidateField(field.Children.front(), std::format("{}[]", path), errors);
            break;
        case DataFieldKind::Enum:
        {
            if (field.EnumChoices.empty())
            {
                errors.push_back(std::format("{}: an enum needs at least one choice", path));
                break;
            }
            std::vector<std::string_view> seen;
            seen.reserve(field.EnumChoices.size());
            for (const DataEnumChoice& choice : field.EnumChoices)
            {
                if (choice.Value.empty())
                {
                    errors.push_back(std::format("{}: an enum choice needs a value", path));
                    continue;
                }
                if (std::ranges::find(seen, std::string_view(choice.Value)) != seen.end())
                    errors.push_back(
                        std::format("{}: duplicate enum choice '{}'", path, choice.Value));
                else
                    seen.push_back(choice.Value);
            }
            break;
        }
        case DataFieldKind::Vector:
            if (field.VectorLength < 2 || field.VectorLength > 4)
                errors.push_back(std::format("{}: a vector is 2, 3 or 4 wide", path));
            break;
        case DataFieldKind::Bool:
        case DataFieldKind::Int:
        case DataFieldKind::Float:
        case DataFieldKind::String:
        case DataFieldKind::AssetRef:
        case DataFieldKind::DataAssetRef:
        case DataFieldKind::GameplayTag:
        case DataFieldKind::Entity:
            break;
        }
    }
}

bool IsValidVerbName(std::string_view name)
{
    if (name.empty())
        return false;

    std::size_t segmentLength = 0;
    for (const char c : name)
    {
        if (c == '.')
        {
            if (segmentLength == 0)
                return false;
            segmentLength = 0;
            continue;
        }
        const bool ok = segmentLength == 0 ? IsNameStart(c) : IsNameBody(c);
        if (!ok)
            return false;
        ++segmentLength;
    }
    return segmentLength != 0;
}

bool VerbContractsMatch(const DataFieldSchema& left, const DataFieldSchema& right)
{
    if (left.Key != right.Key || left.Kind != right.Kind || left.Required != right.Required
        || left.VectorLength != right.VectorLength || left.Default != right.Default)
    {
        return false;
    }
    // Step is presentation: a widget offering coarser increments does not change
    // what the operation accepts, and bumping a revision for it would recompile
    // every binding in the project for a slider tweak.
    if (left.Numeric.Minimum != right.Numeric.Minimum
        || left.Numeric.Maximum != right.Numeric.Maximum)
    {
        return false;
    }
    if (left.Reference.AssetTypeFilter != right.Reference.AssetTypeFilter
        || left.Reference.DataSubtype != right.Reference.DataSubtype)
    {
        return false;
    }
    if (left.EnumChoices.size() != right.EnumChoices.size())
        return false;
    for (std::size_t index = 0; index < left.EnumChoices.size(); ++index)
    {
        // The value is the contract; its label and blurb are not.
        if (left.EnumChoices[index].Value != right.EnumChoices[index].Value)
            return false;
    }
    if (left.Children.size() != right.Children.size())
        return false;
    for (std::size_t index = 0; index < left.Children.size(); ++index)
    {
        if (!VerbContractsMatch(left.Children[index], right.Children[index]))
            return false;
    }
    return true;
}

// ─── VerbRegistrationScope ──────────────────────────────────────────────────

VerbRegistrationScope::VerbRegistrationScope(VerbRegistry& registry, std::string provider)
    : Registry(registry)
    , Provider(std::move(provider))
{
    if (Provider.empty())
        Errors_.emplace_back("a verb registration scope needs a provider name");
}

bool VerbRegistrationScope::Declare(VerbDefinition definition)
{
    const std::size_t before = Errors_.size();

    if (!IsValidVerbName(definition.Name))
    {
        Errors_.push_back(std::format(
            "'{}' is not a verb name: dot-separated identifier segments, no empty segment",
            definition.Name));
    }
    for (const VerbDefinition& pending : Pending)
    {
        if (pending.Name == definition.Name)
        {
            Errors_.push_back(std::format("'{}' is declared twice by provider '{}'",
                                          definition.Name, Provider));
            break;
        }
    }
    if (definition.Arguments.Kind != DataFieldKind::Record)
    {
        Errors_.push_back(std::format("'{}': the argument root is a record", definition.Name));
    }
    else
    {
        ValidateField(definition.Arguments, definition.Name, Errors_);
    }

    if (Errors_.size() != before)
        return false;

    Pending.push_back(std::move(definition));
    return true;
}

bool VerbRegistrationScope::Commit()
{
    if (Committed)
    {
        Errors_.emplace_back("a verb registration scope commits once");
        return false;
    }
    Committed = true;

    if (Errors_.empty() && Registry.Publish(Provider, Pending, Errors_))
        return true;

    // Left where the host will find them. A scope commits once, so this cannot
    // double-report, and a provider that ignores this return value has still
    // made the failure visible to whoever owns startup.
    Registry.InstallationErrors_.insert(Registry.InstallationErrors_.end(), Errors_.begin(),
                                        Errors_.end());
    return false;
}

// ─── VerbRegistry ───────────────────────────────────────────────────────────

VerbRegistry::VerbRegistry()
    : Catalog_{ NextCatalogNumber() }
{
}

bool VerbRegistry::Publish(std::string_view provider,
                           std::vector<VerbDefinition>& definitions,
                           std::vector<std::string>& errors)
{
    // Checked against the live catalog before anything is written, because a
    // half-applied batch hands out ids for verbs the provider never
    // successfully declared and leaves no way back.
    const std::size_t before = errors.size();
    for (const VerbDefinition& definition : definitions)
    {
        const auto it = IdsByName.find(definition.Name);
        if (it == IdsByName.end())
            continue;

        Slot& slot = Slots[IndexOf(it->second)];
        if (slot.State == SlotState::Retired)
            continue;
        if (slot.Provider != provider)
        {
            errors.push_back(std::format(
                "'{}' is already declared by provider '{}'; provider '{}' cannot redeclare it",
                definition.Name, slot.Provider, provider));
        }
    }
    if (errors.size() != before)
        return false;

    for (VerbDefinition& definition : definitions)
    {
        const auto it = IdsByName.find(definition.Name);
        if (it == IdsByName.end())
        {
            Slot slot;
            slot.Definition = std::move(definition);
            slot.Provider = std::string(provider);
            Slots.push_back(std::move(slot));
            IdsByName.emplace(Slots.back().Definition.Name,
                              VerbId{ static_cast<std::uint32_t>(Slots.size()) });
            continue;
        }

        Slot& slot = Slots[IndexOf(it->second)];
        // A revived name is never assumed to mean what it meant before, and a
        // changed contract invalidates the bindings compiled against the old
        // one. Both move the revision; rewording a label does not.
        const bool revived = slot.State == SlotState::Retired;
        const bool contractChanged =
            !VerbContractsMatch(slot.Definition.Arguments, definition.Arguments);
        if (revived || contractChanged)
            slot.Revision = VerbContractRevision{ slot.Revision.Value + 1 };

        slot.Definition = std::move(definition);
        slot.Provider = std::string(provider);
        slot.State = SlotState::Live;
    }
    return true;
}

VerbId VerbRegistry::Find(std::string_view name) const
{
    const auto it = IdsByName.find(std::string(name));
    if (it == IdsByName.end())
        return {};
    return Slots[IndexOf(it->second)].State == SlotState::Live ? it->second : VerbId{};
}

const VerbDefinition* VerbRegistry::Get(VerbId id) const
{
    if (!IsLive(id))
        return nullptr;
    return &Slots[IndexOf(id)].Definition;
}

bool VerbRegistry::IsLive(VerbId id) const
{
    if (!id.IsValid() || IndexOf(id) >= Slots.size())
        return false;
    return Slots[IndexOf(id)].State == SlotState::Live;
}

VerbContractRevision VerbRegistry::Revision(VerbId id) const
{
    if (!IsLive(id))
        return {};
    return Slots[IndexOf(id)].Revision;
}

std::string_view VerbRegistry::Provider(VerbId id) const
{
    if (!IsLive(id))
        return {};
    return Slots[IndexOf(id)].Provider;
}

void VerbRegistry::RetireProvider(std::string_view provider)
{
    for (Slot& slot : Slots)
    {
        if (slot.State != SlotState::Live || slot.Provider != provider)
            continue;
        slot.State = SlotState::Retired;
        slot.Provider.clear();
    }
}

std::vector<VerbId> VerbRegistry::LiveVerbs() const
{
    std::vector<VerbId> live;
    live.reserve(Slots.size());
    for (std::size_t index = 0; index < Slots.size(); ++index)
    {
        if (Slots[index].State == SlotState::Live)
            live.push_back(VerbId{ static_cast<std::uint32_t>(index + 1) });
    }
    return live;
}
