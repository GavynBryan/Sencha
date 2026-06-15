#pragma once

#include <core/console/ConsoleTypes.h>

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class ConsoleOverrideScope;

class ConsoleRegistry
{
public:
    bool RegisterCVar(CVarMetadata metadata, ConsoleResult* result = nullptr);
    bool RegisterCommand(ConsoleCommandMetadata metadata, ConsoleResult* result = nullptr);
    void UnregisterOwner(std::string_view owner);

    [[nodiscard]] CVarMetadata* FindCVar(std::string_view name);
    [[nodiscard]] const CVarMetadata* FindCVar(std::string_view name) const;
    [[nodiscard]] const ConsoleCommandMetadata* FindCommand(std::string_view name) const;

    ConsoleResult SetCVar(std::string_view name,
                          const CVarValue& value,
                          ConsoleValueSource source,
                          ConsolePhase phase,
                          bool force = false);
    ConsoleResult SetCVarFromString(std::string_view name,
                                    std::string_view value,
                                    ConsoleValueSource source,
                                    ConsolePhase phase,
                                    bool force = false);
    ConsoleResult ResetCVar(std::string_view name, ConsolePhase phase);
    ConsoleResult ResetTree(std::string_view prefix, ConsolePhase phase);

    void QueuePendingAssignment(std::string name,
                                std::string valueText,
                                ConsoleValueSource source);
    std::vector<PendingCVarAssignment> ConsumeUnresolvedPendingDiagnostics(std::string_view prefix = {});
    [[nodiscard]] const std::vector<PendingCVarAssignment>& PendingAssignments() const
    {
        return Pending;
    }

    [[nodiscard]] std::vector<const CVarMetadata*> ListCVars(std::string_view prefix = {},
                                                             bool includeHidden = false) const;
    [[nodiscard]] std::vector<const ConsoleCommandMetadata*> ListCommands(
        std::string_view prefix = {},
        bool includeHidden = false) const;
    [[nodiscard]] std::vector<std::string> Complete(std::string_view prefix,
                                                    bool includeHidden = false) const;
    [[nodiscard]] const std::vector<std::string>& TreeNodes() const { return TreeIndex; }

    [[nodiscard]] ConsoleOverrideScope CreateOverrideScope(ConsolePhase phase);

private:
    friend class ConsoleOverrideScope;

    struct CVarRecord
    {
        CVarMetadata Metadata;
    };

    struct CommandRecord
    {
        ConsoleCommandMetadata Metadata;
    };

    void RebuildTree();
    void ApplyPendingFor(CVarRecord& record);
    ConsoleResult SetRecord(CVarRecord& record,
                            const CVarValue& value,
                            ConsoleValueSource source,
                            ConsolePhase phase,
                            bool force);

    std::unordered_map<std::string, CVarRecord> CVars;
    std::unordered_map<std::string, CommandRecord> Commands;
    std::vector<PendingCVarAssignment> Pending;
    std::vector<std::string> TreeIndex;
};

class ConsoleOverrideScope
{
public:
    ConsoleOverrideScope() = default;
    ConsoleOverrideScope(ConsoleRegistry& registry, ConsolePhase phase);
    ~ConsoleOverrideScope();

    ConsoleOverrideScope(const ConsoleOverrideScope&) = delete;
    ConsoleOverrideScope& operator=(const ConsoleOverrideScope&) = delete;
    ConsoleOverrideScope(ConsoleOverrideScope&& other) noexcept;
    ConsoleOverrideScope& operator=(ConsoleOverrideScope&& other) noexcept;

    ConsoleResult Set(std::string_view name, const CVarValue& value, ConsoleValueSource source = {});
    ConsoleResult SetFromString(std::string_view name,
                                std::string_view value,
                                ConsoleValueSource source = {});
    void Restore();

private:
    struct Snapshot
    {
        std::string Name;
        CVarValue Value;
        std::optional<CVarValue> LatchedValue;
        ConsoleValueSource Source;
    };

    ConsoleRegistry* Registry = nullptr;
    ConsolePhase Phase = ConsolePhase::EngineReady;
    std::vector<Snapshot> Snapshots;
};

[[nodiscard]] bool IsValidConsoleName(std::string_view name);
[[nodiscard]] std::string CanonicalConsoleName(std::string_view name);
[[nodiscard]] std::optional<CVarValue> ParseCVarValue(CVarType type, std::string_view text);

