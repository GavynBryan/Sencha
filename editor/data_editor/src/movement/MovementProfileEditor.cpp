#include "movement/MovementProfileEditor.h"

#include "data/DataDocument.h"
#include "data/DataDocumentSet.h"
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

    [[nodiscard]] FieldEdit DrawForm(DataSubtypeFormContext& ctx) override
    {
        return DrawMovementProfileForm(ctx.Data, ctx.Schema, ctx.Documents, Preview);
    }

    void UpdateForFrame(const DataDocument& document,
                        DataDocumentSet& documents) override
    {
        // Kept current whether or not the panels reading it are visible, so
        // opening one mid-edit shows the profile as it stands rather than as it
        // was when the panel last drew.
        Preview.Update(document, documents.Store().Types());
    }

    [[nodiscard]] std::vector<std::unique_ptr<IEditorPanel>>
    CreatePanels(DataDocumentSet& documents) override
    {
        // Both panels read the same dialled-in context as the form, so the
        // preview has one owner rather than a copy per surface.
        std::vector<std::unique_ptr<IEditorPanel>> panels;
        panels.push_back(std::make_unique<MovementResolvePanel>(documents, Preview));
        panels.push_back(std::make_unique<MovementResponsePanel>(documents, Preview));
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
