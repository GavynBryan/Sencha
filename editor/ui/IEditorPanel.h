#pragma once

#include <string_view>

struct IEditorPanel
{
    virtual std::string_view GetTitle() const = 0;
    virtual bool IsVisible() const = 0;
    virtual void OnDraw() = 0;
    virtual ~IEditorPanel() = default;
};
