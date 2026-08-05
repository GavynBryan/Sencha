#include "input/InputProfileEditor.h"

#include "input/InputControlCapture.h"
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
        return DrawInputProfileForm(ctx.Data, ctx.Schema, ctx.Workspace, Preview, Capture);
    }

    void UpdateForFrame(const DataDocument& document,
                        DataEditorWorkspace& workspace) override
    {
        Preview.Update(document, workspace);
    }

    // Claimed before the UI sees it: a control being bound must not also
    // activate whatever widget has focus.
    bool HandlePlatformEvent(const SDL_Event& event) override
    {
        return Capture.HandleSdlEvent(event);
    }

private:
    InputProfilePreview Preview;
    InputControlCapture Capture;
};
}

std::unique_ptr<IDataSubtypeEditor> CreateInputProfileEditor()
{
    return std::make_unique<InputProfileEditor>();
}
