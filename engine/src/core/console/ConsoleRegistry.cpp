#include <core/console/ConsoleRegistry.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>
#include <unordered_set>

namespace
{
    bool IsNameChar(char c)
    {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
    }

    bool StartsWith(std::string_view text, std::string_view prefix)
    {
        return prefix.empty() || (text.size() >= prefix.size()
            && text.substr(0, prefix.size()) == prefix);
    }

    ConsoleResult Failure(ConsoleStatus status, std::string message)
    {
        ConsoleResult result;
        result.Status = status;
        result.Error(std::move(message));
        return result;
    }

    CVarValidationResult ValidateRangeAndEnum(const CVarMetadata& metadata,
                                              const CVarValue& value)
    {
        if (!metadata.EnumValues.empty())
        {
            const std::string text = ToString(value);
            if (std::find(metadata.EnumValues.begin(), metadata.EnumValues.end(), text)
                == metadata.EnumValues.end())
            {
                return CVarValidationResult::Reject(
                    "value '" + text + "' is not in the allowed enum set");
            }
        }

        if (metadata.Min.has_value() || metadata.Max.has_value())
        {
            double numeric = 0.0;
            if (std::holds_alternative<std::int64_t>(value))
                numeric = static_cast<double>(std::get<std::int64_t>(value));
            else if (std::holds_alternative<double>(value))
                numeric = std::get<double>(value);
            else
                return CVarValidationResult::Reject("range validation requires a numeric cvar");

            if (metadata.Min.has_value() && numeric < *metadata.Min)
                return CVarValidationResult::Reject("value is below minimum");
            if (metadata.Max.has_value() && numeric > *metadata.Max)
                return CVarValidationResult::Reject("value is above maximum");
        }

        if (metadata.Validator)
            return metadata.Validator(value);
        return CVarValidationResult::Ok();
    }
}

bool IsValidConsoleName(std::string_view name)
{
    if (name.empty() || name.front() == '.' || name.back() == '.')
        return false;

    bool previousDot = false;
    for (char c : name)
    {
        if (c == '.')
        {
            if (previousDot)
                return false;
            previousDot = true;
            continue;
        }
        if (!IsNameChar(c))
            return false;
        previousDot = false;
    }
    return true;
}

std::string CanonicalConsoleName(std::string_view name)
{
    std::string result;
    result.reserve(name.size());
    for (char c : name)
        result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return result;
}

std::optional<CVarValue> ParseCVarValue(CVarType type, std::string_view text)
{
    std::string s(text);
    switch (type)
    {
    case CVarType::Bool:
    {
        std::string lower = CanonicalConsoleName(s);
        if (lower == "1" || lower == "true" || lower == "on" || lower == "yes")
            return CVarValue(true);
        if (lower == "0" || lower == "false" || lower == "off" || lower == "no")
            return CVarValue(false);
        return std::nullopt;
    }
    case CVarType::Int:
    {
        try
        {
            std::size_t pos = 0;
            const long long value = std::stoll(s, &pos, 10);
            if (pos != s.size())
                return std::nullopt;
            return CVarValue(static_cast<std::int64_t>(value));
        }
        catch (...)
        {
            return std::nullopt;
        }
    }
    case CVarType::Double:
    {
        try
        {
            std::size_t pos = 0;
            const double value = std::stod(s, &pos);
            if (pos != s.size() || !std::isfinite(value))
                return std::nullopt;
            return CVarValue(value);
        }
        catch (...)
        {
            return std::nullopt;
        }
    }
    case CVarType::String:
        return CVarValue(std::move(s));
    }
    return std::nullopt;
}

bool ConsoleRegistry::RegisterCVar(CVarMetadata metadata, ConsoleResult* result)
{
    metadata.Name = CanonicalConsoleName(metadata.Name);
    if (metadata.Owner.empty())
        metadata.Owner = "engine";
    if (metadata.Source.Description.empty())
        metadata.Source.Description = "registration";

    if (!IsValidConsoleName(metadata.Name))
    {
        if (result)
            *result = Failure(ConsoleStatus::InvalidArguments,
                              "invalid cvar name '" + metadata.Name + "'");
        return false;
    }
    if (CVars.find(metadata.Name) != CVars.end()
        || Commands.find(metadata.Name) != Commands.end())
    {
        if (result)
            *result = Failure(ConsoleStatus::InvalidArguments,
                              "console name already registered: " + metadata.Name);
        return false;
    }
    if (TypeOf(metadata.DefaultValue) != metadata.Type
        || TypeOf(metadata.CurrentValue) != metadata.Type)
    {
        if (result)
            *result = Failure(ConsoleStatus::InvalidArguments,
                              "cvar default/current type mismatch for " + metadata.Name);
        return false;
    }

    const std::string key = metadata.Name;
    auto [it, inserted] = CVars.emplace(key, CVarRecord{ std::move(metadata) });
    (void)inserted;
    ApplyPendingFor(it->second);
    RebuildTree();
    if (result)
        result->Info("registered cvar " + it->second.Metadata.Name);
    return true;
}

bool ConsoleRegistry::RegisterCommand(ConsoleCommandMetadata metadata, ConsoleResult* result)
{
    metadata.Name = CanonicalConsoleName(metadata.Name);
    if (metadata.Owner.empty())
        metadata.Owner = "engine";

    if (!IsValidConsoleName(metadata.Name))
    {
        if (result)
            *result = Failure(ConsoleStatus::InvalidArguments,
                              "invalid command name '" + metadata.Name + "'");
        return false;
    }
    if (!metadata.Callback)
    {
        if (result)
            *result = Failure(ConsoleStatus::InvalidArguments,
                              "command has no callback: " + metadata.Name);
        return false;
    }
    if (Commands.find(metadata.Name) != Commands.end()
        || CVars.find(metadata.Name) != CVars.end())
    {
        if (result)
            *result = Failure(ConsoleStatus::InvalidArguments,
                              "console name already registered: " + metadata.Name);
        return false;
    }

    const std::string key = metadata.Name;
    Commands.emplace(key, CommandRecord{ std::move(metadata) });
    RebuildTree();
    if (result)
        result->Info("registered command");
    return true;
}

void ConsoleRegistry::UnregisterOwner(std::string_view owner)
{
    for (auto it = CVars.begin(); it != CVars.end();)
    {
        if (it->second.Metadata.Owner == owner)
            it = CVars.erase(it);
        else
            ++it;
    }
    for (auto it = Commands.begin(); it != Commands.end();)
    {
        if (it->second.Metadata.Owner == owner)
            it = Commands.erase(it);
        else
            ++it;
    }
    RebuildTree();
}

CVarMetadata* ConsoleRegistry::FindCVar(std::string_view name)
{
    const std::string key = CanonicalConsoleName(name);
    auto it = CVars.find(key);
    if (it != CVars.end())
        return &it->second.Metadata;
    for (auto& [registered, record] : CVars)
        if (registered == key)
            return &record.Metadata;
    return nullptr;
}

const CVarMetadata* ConsoleRegistry::FindCVar(std::string_view name) const
{
    const std::string key = CanonicalConsoleName(name);
    auto it = CVars.find(key);
    if (it != CVars.end())
        return &it->second.Metadata;
    for (const auto& [registered, record] : CVars)
        if (registered == key)
            return &record.Metadata;
    return nullptr;
}

const ConsoleCommandMetadata* ConsoleRegistry::FindCommand(std::string_view name) const
{
    const std::string key = CanonicalConsoleName(name);
    auto it = Commands.find(key);
    if (it != Commands.end())
        return &it->second.Metadata;
    for (const auto& [registered, record] : Commands)
        if (registered == key)
            return &record.Metadata;
    return nullptr;
}

ConsoleResult ConsoleRegistry::SetCVar(std::string_view name,
                                       const CVarValue& value,
                                       ConsoleValueSource source,
                                       ConsolePhase phase,
                                       bool force)
{
    const std::string key = CanonicalConsoleName(name);
    auto it = CVars.find(key);
    if (it == CVars.end())
    {
        QueuePendingAssignment(key, ToString(value), std::move(source));
        ConsoleResult result;
        result.Status = ConsoleStatus::Deferred;
        result.Warning("cvar '" + key + "' is not registered yet; assignment is pending");
        return result;
    }
    return SetRecord(it->second, value, std::move(source), phase, force);
}

ConsoleResult ConsoleRegistry::SetCVarFromString(std::string_view name,
                                                 std::string_view value,
                                                 ConsoleValueSource source,
                                                 ConsolePhase phase,
                                                 bool force)
{
    const std::string key = CanonicalConsoleName(name);
    auto it = CVars.find(key);
    if (it == CVars.end())
    {
        QueuePendingAssignment(key, std::string(value), std::move(source));
        ConsoleResult result;
        result.Status = ConsoleStatus::Deferred;
        result.Warning("cvar '" + key + "' is not registered yet; assignment is pending");
        return result;
    }

    std::optional<CVarValue> parsed = ParseCVarValue(it->second.Metadata.Type, value);
    if (!parsed)
        return Failure(ConsoleStatus::InvalidArguments, "invalid value for cvar '" + key + "'");
    return SetRecord(it->second, *parsed, std::move(source), phase, force);
}

ConsoleResult ConsoleRegistry::ResetCVar(std::string_view name, ConsolePhase phase)
{
    CVarMetadata* metadata = FindCVar(name);
    if (metadata == nullptr)
        return Failure(ConsoleStatus::InvalidArguments,
                       "unknown cvar '" + CanonicalConsoleName(name) + "'");
    return SetCVar(metadata->Name, metadata->DefaultValue, { "reset" }, phase, true);
}

ConsoleResult ConsoleRegistry::ResetTree(std::string_view prefix, ConsolePhase phase)
{
    const std::string canonical = CanonicalConsoleName(prefix);
    ConsoleResult result;
    std::size_t count = 0;
    for (auto& [name, record] : CVars)
    {
        if (!StartsWith(name, canonical))
            continue;
        ConsoleResult one = SetRecord(record, record.Metadata.DefaultValue, { "reset_tree" },
                                      phase, true);
        if (!one.Succeeded())
        {
            result.Status = one.Status;
            result.Output.insert(result.Output.end(), one.Output.begin(), one.Output.end());
        }
        else
        {
            ++count;
        }
    }
    result.Info("reset " + std::to_string(count) + " cvar(s)");
    return result;
}

void ConsoleRegistry::QueuePendingAssignment(std::string name,
                                             std::string valueText,
                                             ConsoleValueSource source)
{
    name = CanonicalConsoleName(name);
    Pending.push_back({ std::move(name), std::move(valueText), std::move(source) });
}

std::vector<PendingCVarAssignment> ConsoleRegistry::ConsumeUnresolvedPendingDiagnostics(
    std::string_view prefix)
{
    const std::string canonical = CanonicalConsoleName(prefix);
    std::vector<PendingCVarAssignment> unresolved;
    for (PendingCVarAssignment& pending : Pending)
    {
        if (!pending.Resolved && StartsWith(pending.Name, canonical))
        {
            if (pending.Diagnostic.empty())
                pending.Diagnostic = "cvar was never registered";
            unresolved.push_back(pending);
        }
    }
    return unresolved;
}

std::vector<const CVarMetadata*> ConsoleRegistry::ListCVars(std::string_view prefix,
                                                            bool includeHidden) const
{
    const std::string canonical = CanonicalConsoleName(prefix);
    std::vector<const CVarMetadata*> result;
    for (const auto& [name, record] : CVars)
    {
        if (!StartsWith(name, canonical))
            continue;
        if (!includeHidden && HasFlag(record.Metadata.Flags, CVarFlags::Hidden))
            continue;
        result.push_back(&record.Metadata);
    }
    std::sort(result.begin(), result.end(), [](const CVarMetadata* a, const CVarMetadata* b) {
        return a->Name < b->Name;
    });
    return result;
}

std::vector<const ConsoleCommandMetadata*> ConsoleRegistry::ListCommands(
    std::string_view prefix,
    bool includeHidden) const
{
    const std::string canonical = CanonicalConsoleName(prefix);
    std::vector<const ConsoleCommandMetadata*> result;
    for (const auto& [name, record] : Commands)
    {
        if (!StartsWith(name, canonical))
            continue;
        if (!includeHidden && HasFlag(record.Metadata.Flags, ConsoleCommandFlags::Hidden))
            continue;
        result.push_back(&record.Metadata);
    }
    std::sort(result.begin(), result.end(), [](const auto* a, const auto* b) {
        return a->Name < b->Name;
    });
    return result;
}

std::vector<std::string> ConsoleRegistry::Complete(std::string_view prefix,
                                                   bool includeHidden) const
{
    std::vector<std::string> result;
    for (const CVarMetadata* cvar : ListCVars(prefix, includeHidden))
        result.push_back(cvar->Name);
    for (const ConsoleCommandMetadata* command : ListCommands(prefix, includeHidden))
        result.push_back(command->Name);
    for (const std::string& node : TreeIndex)
    {
        if (StartsWith(node, CanonicalConsoleName(prefix)))
            result.push_back(node);
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

ConsoleOverrideScope ConsoleRegistry::CreateOverrideScope(ConsolePhase phase)
{
    return ConsoleOverrideScope(*this, phase);
}

void ConsoleRegistry::RebuildTree()
{
    std::unordered_set<std::string> nodes;
    auto addNodes = [&nodes](const std::string& name) {
        std::size_t dot = name.find('.');
        while (dot != std::string::npos)
        {
            nodes.insert(name.substr(0, dot));
            dot = name.find('.', dot + 1);
        }
    };
    for (const auto& [name, _] : CVars)
        addNodes(name);
    for (const auto& [name, _] : Commands)
        addNodes(name);
    TreeIndex.assign(nodes.begin(), nodes.end());
    std::sort(TreeIndex.begin(), TreeIndex.end());
}

void ConsoleRegistry::ApplyPendingFor(CVarRecord& record)
{
    for (PendingCVarAssignment& pending : Pending)
    {
        if (pending.Resolved || pending.Name != record.Metadata.Name)
            continue;

        std::optional<CVarValue> value =
            ParseCVarValue(record.Metadata.Type, pending.ValueText);
        if (!value)
        {
            pending.Diagnostic = "pending value could not be parsed for registered cvar";
            continue;
        }

        ConsoleResult applied = SetRecord(record, *value, pending.Source,
                                          ConsolePhase::EngineReady, false);
        if (applied.Succeeded() || applied.Status == ConsoleStatus::Deferred)
            pending.Resolved = true;
        else if (!applied.Output.empty())
            pending.Diagnostic = applied.Output.front().Text;
    }
}

ConsoleResult ConsoleRegistry::SetRecord(CVarRecord& record,
                                         const CVarValue& value,
                                         ConsoleValueSource source,
                                         ConsolePhase phase,
                                         bool force)
{
    CVarMetadata& metadata = record.Metadata;
    if (TypeOf(value) != metadata.Type)
        return Failure(ConsoleStatus::InvalidArguments, "cvar type mismatch: " + metadata.Name);
    if (!force && HasFlag(metadata.Flags, CVarFlags::ReadOnly))
        return Failure(ConsoleStatus::ValidationFailed, "cvar is read-only: " + metadata.Name);
    if (!force && HasFlag(metadata.Flags, CVarFlags::InitOnly)
        && IsPhaseReady(phase, ConsolePhase::SystemsRegistered))
    {
        return Failure(ConsoleStatus::PhaseNotReady, "cvar is init-only: " + metadata.Name);
    }

    CVarValidationResult validation = ValidateRangeAndEnum(metadata, value);
    if (!validation.Accepted)
        return Failure(ConsoleStatus::ValidationFailed,
                       metadata.Name + ": " + validation.Message);

    if (!force && HasFlag(metadata.Flags, CVarFlags::Latched)
        && IsPhaseReady(phase, ConsolePhase::SystemsRegistered))
    {
        metadata.LatchedValue = value;
        metadata.PendingValue = value;
        metadata.Source = std::move(source);
        ConsoleResult result;
        result.Status = ConsoleStatus::Deferred;
        result.Info("latched " + metadata.Name + " = " + ToString(value));
        return result;
    }

    const CVarValue old = metadata.CurrentValue;
    metadata.CurrentValue = value;
    metadata.PendingValue.reset();
    metadata.LatchedValue.reset();
    metadata.Source = std::move(source);

    if (metadata.OnChange && old != metadata.CurrentValue)
    {
        CVarChangeContext ctx{
            .Name = metadata.Name,
            .OldValue = old,
            .NewValue = metadata.CurrentValue,
            .Source = metadata.Source,
            .Phase = phase,
        };
        metadata.OnChange(ctx);
    }

    ConsoleResult result;
    result.Info(metadata.Name + " = " + ToString(metadata.CurrentValue));
    return result;
}

ConsoleOverrideScope::ConsoleOverrideScope(ConsoleRegistry& registry, ConsolePhase phase)
    : Registry(&registry)
    , Phase(phase)
{
}

ConsoleOverrideScope::~ConsoleOverrideScope()
{
    Restore();
}

ConsoleOverrideScope::ConsoleOverrideScope(ConsoleOverrideScope&& other) noexcept
    : Registry(other.Registry)
    , Phase(other.Phase)
    , Snapshots(std::move(other.Snapshots))
{
    other.Registry = nullptr;
}

ConsoleOverrideScope& ConsoleOverrideScope::operator=(ConsoleOverrideScope&& other) noexcept
{
    if (this != &other)
    {
        Restore();
        Registry = other.Registry;
        Phase = other.Phase;
        Snapshots = std::move(other.Snapshots);
        other.Registry = nullptr;
    }
    return *this;
}

ConsoleResult ConsoleOverrideScope::Set(std::string_view name,
                                        const CVarValue& value,
                                        ConsoleValueSource source)
{
    if (Registry == nullptr)
        return Failure(ConsoleStatus::ExecutionFailed, "override scope is inactive");

    CVarMetadata* metadata = Registry->FindCVar(name);
    if (metadata == nullptr)
        return Failure(ConsoleStatus::InvalidArguments,
                       "unknown cvar '" + CanonicalConsoleName(name) + "'");

    const std::string key = metadata->Name;
    if (std::none_of(Snapshots.begin(), Snapshots.end(), [&key](const Snapshot& s) {
        return s.Name == key;
    }))
    {
        Snapshots.push_back({
            .Name = key,
            .Value = metadata->CurrentValue,
            .LatchedValue = metadata->LatchedValue,
            .Source = metadata->Source,
        });
    }

    if (source.Description.empty())
        source.Description = "override";
    return Registry->SetCVar(key, value, std::move(source), Phase, true);
}

ConsoleResult ConsoleOverrideScope::SetFromString(std::string_view name,
                                                  std::string_view value,
                                                  ConsoleValueSource source)
{
    CVarMetadata* metadata = Registry ? Registry->FindCVar(name) : nullptr;
    if (metadata == nullptr)
        return Failure(ConsoleStatus::InvalidArguments,
                       "unknown cvar '" + CanonicalConsoleName(name) + "'");
    std::optional<CVarValue> parsed = ParseCVarValue(metadata->Type, value);
    if (!parsed)
        return Failure(ConsoleStatus::InvalidArguments, "invalid override value");
    return Set(name, *parsed, std::move(source));
}

void ConsoleOverrideScope::Restore()
{
    if (Registry == nullptr)
        return;

    for (auto it = Snapshots.rbegin(); it != Snapshots.rend(); ++it)
    {
        CVarMetadata* metadata = Registry->FindCVar(it->Name);
        if (metadata == nullptr)
            continue;
        metadata->LatchedValue = it->LatchedValue;
        metadata->Source = it->Source;
        (void)Registry->SetCVar(it->Name, it->Value, it->Source, Phase, true);
    }
    Snapshots.clear();
    Registry = nullptr;
}
