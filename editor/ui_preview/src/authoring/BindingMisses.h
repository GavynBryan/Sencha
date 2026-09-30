#pragma once

#include <ui/UiDiagnostic.h>

#include <span>
#include <string>
#include <vector>

struct UiPreviewModel;

//=============================================================================
// BindingMisses
//
// The names a document asked its model for and the model did not declare, as
// the document engine reported them -- runtime-observed, not statically
// complete. A binding behind a branch that has not been true yet has not been
// observed; an action is observed only when its event fires. The sidecar stays
// the schema; this is the list of what it is missing so far.
//=============================================================================
enum class BindingMissKind : std::uint8_t
{
    // A top-level data variable: a property, a list or a row list.
    Variable,
    // A member reached through a list element (`rows[0].label`) or a
    // struct: the row shape lacks it, or the row list is undeclared.
    Member,
    // An event callback: an action.
    Action,
};

struct BindingMiss
{
    BindingMissKind Kind = BindingMissKind::Variable;
    std::string Name;
    // Member: the list variable the member was read through, when the report
    // said (`rows` for `rows[0].label`); empty when it did not.
    std::string Parent;

    bool operator==(const BindingMiss&) const = default;
};

// Distinct misses across `diagnostics`, in first-seen order, excluding names
// `model` already declares.
[[nodiscard]] std::vector<BindingMiss> CollectBindingMisses(std::span<const UiDiagnostic> diagnostics,
                                                            const UiPreviewModel& model);
