#pragma once

#include "properties/PropertySource.h"

#include <functional>
#include <string>
#include <string_view>

//=============================================================================
// PropertyEditSession
//
// The one edit in flight in a presentation, and the thing every interruption
// ends: Escape, focus leaving, a tool switch, the workspace hiding, the
// document closing, the selection moving on. Beginning another edit ends the
// open one first, so two can never be staged at once.
//=============================================================================
class PropertyEditSession
{
public:
    ~PropertyEditSession() { Cancel(); }

    bool Begin(IPropertySource& source, std::string key);
    PropertyEditResult Preview(std::string_view value);
    PropertyEditResult PreviewBy(double delta);
    // Ends the session whatever the answer.
    PropertyEditResult Commit(std::string_view value);
    PropertyEditResult CommitBy(double delta);
    void Cancel();

    // A value set in one go, with no preview: a field typed and entered, a
    // choice picked. Ends any open edit first.
    PropertyEditResult Set(IPropertySource& source, std::string key, std::string_view value);

    [[nodiscard]] bool IsOpen() const { return Source != nullptr; }
    [[nodiscard]] const std::string& Key() const { return EditingKey; }

private:
    PropertyEditResult Finish(const std::function<PropertyEditResult(IPropertySource&, std::string_view)>& commit);

    IPropertySource* Source = nullptr;
    std::string EditingKey;
};
