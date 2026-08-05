#include "input/InputActionSetEditor.h"

#include "input/InputActionSetForm.h"

namespace
{
class InputActionSetEditor final : public IDataSubtypeEditor
{
public:
    [[nodiscard]] std::string_view Subtype() const override
    {
        return InputActionSetSubtype();
    }

    [[nodiscard]] FieldEdit DrawForm(SubtypeFormContext& ctx) override
    {
        return DrawInputActionSetForm(ctx.Data, ctx.Schema, ctx.Workspace);
    }
};
}

std::unique_ptr<IDataSubtypeEditor> CreateInputActionSetEditor()
{
    return std::make_unique<InputActionSetEditor>();
}
