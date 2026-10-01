#pragma once

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Geometry.h>

// `<sector>`: an antialiased annular sector filling the element's box, painted
// by `fill-color` and `stroke-color`/`stroke-width` (docs/ui/architecture.md §7).
class RmlSectorElement final : public Rml::Element
{
public:
    explicit RmlSectorElement(const Rml::String& name);

    // Once per initialisation of the document engine, after it.
    static void Register();

protected:
    void OnRender() override;
    void OnAttributeChange(const Rml::ElementAttributes& changed) override;
    void OnPropertyChange(const Rml::PropertyIdSet& changed) override;

private:
    void Generate(Rml::Vector2f size);

    Rml::Geometry Shape;
    Rml::Vector2f GeneratedSize{ -1.0f, -1.0f };
    bool Dirty = true;
};
