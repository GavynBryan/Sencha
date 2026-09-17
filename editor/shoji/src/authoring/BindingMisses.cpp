#include "authoring/BindingMisses.h"

#include "authoring/UiPreviewModel.h"

#include <algorithm>

namespace
{
    // `rows[0].label` -> parent "rows", member "label"; `row.label` (an
    // iterator alias the engine already resolved) arrives as the former.
    BindingMiss ClassifyVariable(const std::string& variable)
    {
        const std::size_t dot = variable.rfind('.');
        if (dot == std::string::npos)
            return BindingMiss{ BindingMissKind::Variable, variable, {} };
        std::string parent = variable.substr(0, dot);
        if (const std::size_t bracket = parent.find('['); bracket != std::string::npos)
            parent.erase(bracket);
        return BindingMiss{ BindingMissKind::Member, variable.substr(dot + 1), parent };
    }
}

std::vector<BindingMiss> CollectBindingMisses(std::span<const UiDiagnostic> diagnostics,
                                              const UiPreviewModel& model)
{
    std::vector<BindingMiss> out;
    for (const UiDiagnostic& d : diagnostics)
    {
        if (!d.Variable.has_value())
            continue;
        BindingMiss miss;
        if (d.Kind == UiDiagnosticKind::EventCallbackMissing)
        {
            miss = BindingMiss{ BindingMissKind::Action, *d.Variable, {} };
            if (model.DeclaresAction(miss.Name))
                continue;
        }
        else if (d.Kind == UiDiagnosticKind::MemberMissing)
        {
            // The member alone, no list named; a BindingMissing with the full
            // path usually follows and fills the parent in below.
            miss = BindingMiss{ BindingMissKind::Member, *d.Variable, {} };
        }
        else if (d.Kind == UiDiagnosticKind::BindingMissing)
        {
            miss = ClassifyVariable(*d.Variable);
            if (miss.Kind == BindingMissKind::Variable
                && (model.DeclaresProperty(miss.Name) || model.DeclaresArray(miss.Name)
                    || model.DeclaresRows(miss.Name)))
                continue;
        }
        else
        {
            continue;
        }

        const auto same = [&](const BindingMiss& other) {
            if (other.Kind != miss.Kind || other.Name != miss.Name)
                return false;
            // A member reported with and without its parent is the same member.
            return other.Parent.empty() || miss.Parent.empty() || other.Parent == miss.Parent;
        };
        const auto existing = std::find_if(out.begin(), out.end(), same);
        if (existing == out.end())
            out.push_back(std::move(miss));
        else if (existing->Parent.empty())
            existing->Parent = miss.Parent;   // the fuller report fills in what the terser one lacked
    }
    return out;
}
