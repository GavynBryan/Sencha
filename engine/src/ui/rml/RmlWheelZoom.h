#pragma once

#include <RmlUi/Core/EventListener.h>

#include <string_view>

// Stops Ctrl and the wheel inside a `wheel-zoom` element once its listeners have
// heard it, so nothing scrolls (docs/ui/architecture.md §6). Listens on a
// context's root, so it runs after every listener below.
class RmlWheelZoom final : public Rml::EventListener
{
public:
    static constexpr std::string_view kAttribute = "wheel-zoom";

    void ProcessEvent(Rml::Event& event) override;
};
