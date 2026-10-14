#pragma once

#include "authoring/AnimationTraceImport.h"

#include "properties/PropertySource.h"

#include <functional>
#include <optional>
#include <string>

// A trace a running game wrote with anim.trace.export, read back: whose it is
// and which rig it ran (opened through `openRig`), what it did not capture,
// and its decision records filtered by cause, layer or detail.
class AnimationTraceProperties final : public CommitOnlyPropertySource
{
public:
    explicit AnimationTraceProperties(std::function<void(const std::string& rig)> openRig)
        : OpenRig(std::move(openRig))
    {
    }

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;
    PropertyEditResult CommitEdit(std::string_view key, std::string_view value) override;
    PropertyEditResult Act(std::string_view key, std::string_view verb, std::string_view choice) override;

private:
    std::function<void(const std::string& rig)> OpenRig;
    std::string File;
    std::string Filter;
    std::string Error;
    std::optional<AnimationTrace> Trace;
};
