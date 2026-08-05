#include "input/InputProfileEditor.h"

#include "input/InputProfileForm.h"
#include "input/InputProfilePreview.h"

namespace
{
class InputProfileEditor final : public IDataSubtypeEditor
{
public:
    [[nodiscard]] std::string_view Subtype() const override
    {
        return InputProfileSubtype();
    }

    [[nodiscard]] FieldEdit DrawForm(SubtypeFormContext& ctx) override
    {
        return DrawInputProfileForm(ctx.Data, ctx.Schema, ctx.Workspace, Preview);
    }

    void UpdateForFrame(const DataDocument& document,
                        DataEditorWorkspace& workspace) override
    {
        Preview.Update(document, workspace);
    }

private:
    InputProfilePreview Preview;
};
}

std::unique_ptr<IDataSubtypeEditor> CreateInputProfileEditor()
{
    return std::make_unique<InputProfileEditor>();
}
