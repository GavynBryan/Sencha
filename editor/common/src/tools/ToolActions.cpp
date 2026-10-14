#include "tools/ToolActions.h"

#include "actions/ActionOffer.h"
#include "tools/ToolRegistry.h"

#include <SDL3/SDL_keycode.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

void OfferToolActions(ActionCatalog& catalog, ActionContext& context, const ToolRegistryResolver& tools)
{
    const ActionOffers offer{ catalog, context, "Tool" };
    if (ToolRegistry* registry = tools())
        for (const std::unique_ptr<ITool>& tool : registry->GetTools())
        {
            if (tool == nullptr)
                continue;
            const std::string id(tool->GetId());
            const ITool::Shortcut shortcut = tool->GetShortcut();
            std::vector<KeyChord> chords;
            if (shortcut.Key != SDLK_UNKNOWN)
                chords.push_back(KeyChord{ shortcut.Key, shortcut.Mods });
            offer({ .Id = "tool." + id, .Label = std::string(tool->GetDisplayName()), .Icon = tool->GetIcon(),
                    .DefaultChords = std::move(chords) },
                  { .Invoke = [tools, id] {
                        if (ToolRegistry* live = tools())
                            (void)live->Activate(id);
                    } });
        }

    // A wheel's key is held rather than pressed, so its session owns both edges
    // and nothing claims the action; it is defined so the keymap, hints and
    // palette name it like any other.
    offer({ .Id = "tool.wheel", .Label = "Tool Wheel", .DefaultChords = { { SDLK_Q, {} } } }, {});

    // The tools take Enter and Escape themselves, so these carry no chord.
    const auto active = [tools]() -> std::pair<ITool*, ToolRegistry*> {
        ToolRegistry* registry = tools();
        return { registry != nullptr ? registry->GetActiveTool() : nullptr, registry };
    };
    offer({ .Id = "tool.apply", .Label = "Apply", .Icon = IconId::Check },
          { .Invoke = [active] {
                if (const auto [tool, registry] = active(); tool != nullptr && tool->Draft() == ToolDraft::Ready)
                    tool->CommitPending(registry->GetContext());
            } });
    offer({ .Id = "tool.cancel", .Label = "Cancel", .Icon = IconId::Cancel },
          { .Invoke = [active] {
                if (const auto [tool, registry] = active(); tool != nullptr && tool->Draft() != ToolDraft::None)
                    tool->OnCancel(registry->GetContext());
            } });
}

WorkspaceOperationFacet ToolOperation(ToolRegistry* tools, IPropertySource* parameters)
{
    ITool* tool = tools != nullptr ? tools->GetActiveTool() : nullptr;
    if (tool == nullptr)
        return {};
    const std::span<const ITool::Variant> variants = tool->GetVariants();
    const int variant = tool->GetActiveVariant(tools->GetContext());
    std::vector<OperationChoice> choices;
    for (const std::unique_ptr<ITool>& offered : tools->GetTools())
        if (offered != nullptr)
            choices.push_back({ .Label = std::string(offered->GetDisplayName()),
                                .Icon = offered->GetIcon(),
                                .Action = "tool." + std::string(offered->GetId()),
                                .Current = offered.get() == tool });
    return {
        .Name = std::string(tool->GetDisplayName()),
        .Variant = variant >= 0 && variant < static_cast<int>(variants.size())
            ? std::string(variants[static_cast<std::size_t>(variant)].Label)
            : std::string{},
        .Icon = tool->GetIcon(),
        .Draft = tool->Draft(),
        .Hint = tool->Hint(tools->GetContext()),
        .Parameters = parameters,
        .Choices = std::move(choices),
    };
}
