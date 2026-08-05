#include "movement/MovementProfileEditor.h"

#include "DataDocument.h"
#include "DataEditorWorkspace.h"
#include "movement/MovementProfileForm.h"
#include "movement/MovementResolvePanel.h"
#include "movement/MovementResolvePreview.h"
#include "movement/MovementResponsePanel.h"

namespace
{
class MovementProfileEditor final : public IDataSubtypeEditor
{
public:
    [[nodiscard]] std::string_view Subtype() const override
    {
        return MovementProfileSubtype();
    }

    [[nodiscard]] FieldEdit DrawForm(SubtypeFormContext& ctx) override
    {
        return DrawMovementProfileForm(ctx.Data, ctx.Schema, ctx.Workspace, Preview);
    }

    void UpdateForFrame(const DataDocument& document,
                        DataEditorWorkspace& workspace) override
    {
        // Kept current whether or not the panels reading it are visible, so
        // opening one mid-edit shows the profile as it stands rather than as it
        // was when the panel last drew.
        Preview.Update(document, workspace.Types());
    }

    [[nodiscard]] std::vector<std::unique_ptr<IEditorPanel>>
    CreatePanels(DataEditorWorkspace& workspace) override
    {
        // Both panels read the same dialled-in context as the form, so the
        // preview has one owner rather than a copy per surface.
        std::vector<std::unique_ptr<IEditorPanel>> panels;
        panels.push_back(std::make_unique<MovementResolvePanel>(workspace, Preview));
        panels.push_back(std::make_unique<MovementResponsePanel>(workspace, Preview));
        return panels;
    }

private:
    MovementResolvePreview Preview;
};
}

std::unique_ptr<IDataSubtypeEditor> CreateMovementProfileEditor()
{
    return std::make_unique<MovementProfileEditor>();
}
