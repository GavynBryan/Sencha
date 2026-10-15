#include "RmlSectorElement.h"

#include "../SectorMesh.h"

#include <math/Angle.h>

#include <RmlUi/Core/ComputedValues.h>
#include <RmlUi/Core/ElementInstancer.h>
#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/Mesh.h>
#include <RmlUi/Core/PropertyDefinition.h>
#include <RmlUi/Core/RenderManager.h>
#include <RmlUi/Core/StyleSheetSpecification.h>

#include <algorithm>

namespace
{
// One pixel of antialiasing across every edge.
constexpr float kFeather = 1.0f;

Rml::PropertyId FillColor = Rml::PropertyId::Invalid;
Rml::PropertyId StrokeColor = Rml::PropertyId::Invalid;
Rml::PropertyId StrokeWidth = Rml::PropertyId::Invalid;

Rml::ElementInstancerGeneric<RmlSectorElement>& Instancer()
{
    static Rml::ElementInstancerGeneric<RmlSectorElement> instancer;
    return instancer;
}

void AppendColoured(const SectorMesh& shape, Rml::Colourb colour, float opacity, Rml::Mesh& out)
{
    const auto base = static_cast<int>(out.vertices.size());
    for (std::size_t i = 0; i < shape.Positions.size(); ++i)
    {
        Rml::Vertex vertex;
        vertex.position = { shape.Positions[i].X, shape.Positions[i].Y };
        vertex.colour = colour.ToPremultiplied(opacity * shape.Coverage[i]);
        out.vertices.push_back(vertex);
    }
    for (const std::uint32_t index : shape.Indices)
        out.indices.push_back(base + static_cast<int>(index));
}
} // namespace

RmlSectorElement::RmlSectorElement(const Rml::String& name)
    : Rml::Element(name)
{
}

void RmlSectorElement::Register()
{
    FillColor = Rml::StyleSheetSpecification::RegisterProperty("fill-color", "transparent", false)
                    .AddParser("color")
                    .GetId();
    StrokeColor = Rml::StyleSheetSpecification::RegisterProperty("stroke-color", "transparent", false)
                      .AddParser("color")
                      .GetId();
    StrokeWidth = Rml::StyleSheetSpecification::RegisterProperty("stroke-width", "0px", false)
                      .AddParser("length")
                      .GetId();
    Rml::Factory::RegisterElementInstancer("sector", &Instancer());
}

void RmlSectorElement::OnRender()
{
    const Rml::Vector2f size = GetBox().GetSize(Rml::BoxArea::Border);
    if (Dirty || size != GeneratedSize)
        Generate(size);
    Shape.Render(GetAbsoluteOffset(Rml::BoxArea::Border));
}

void RmlSectorElement::OnAttributeChange(const Rml::ElementAttributes& changed)
{
    Rml::Element::OnAttributeChange(changed);
    for (const char* name : { "start", "sweep", "inner" })
        if (changed.find(name) != changed.end())
            Dirty = true;
}

void RmlSectorElement::OnPropertyChange(const Rml::PropertyIdSet& changed)
{
    Rml::Element::OnPropertyChange(changed);
    if (changed.Contains(FillColor) || changed.Contains(StrokeColor) || changed.Contains(StrokeWidth)
        || changed.Contains(Rml::PropertyId::Opacity))
        Dirty = true;
}

void RmlSectorElement::Generate(Rml::Vector2f size)
{
    Dirty = false;
    GeneratedSize = size;
    Rml::RenderManager* render = GetRenderManager();
    if (render == nullptr)
        return;

    const float outer = std::min(size.x, size.y) * 0.5f;
    SectorShape shape{
        .Center = { size.x * 0.5f, size.y * 0.5f },
        .Outer = outer,
        .Inner = std::clamp(GetAttribute<float>("inner", 0.0f), 0.0f, 1.0f) * outer,
        .Start = ToRadians(GetAttribute<float>("start", 0.0f)),
        .Sweep = ToRadians(std::clamp(GetAttribute<float>("sweep", 360.0f), 0.0f, 360.0f)),
    };
    const float opacity = GetComputedValues().opacity();
    const auto colour = [this](Rml::PropertyId property_id) {
        const Rml::Property* property = GetProperty(property_id);
        return property != nullptr ? property->Get<Rml::Colourb>() : Rml::Colourb(0, 0, 0, 0);
    };
    const Rml::Colourb fill = colour(FillColor);
    const Rml::Colourb stroke = colour(StrokeColor);
    const Rml::Property* strokeWidth = GetProperty(StrokeWidth);
    const float width = strokeWidth != nullptr ? std::max(0.0f, ResolveLength(strokeWidth->GetNumericValue())) : 0.0f;

    Rml::Mesh mesh;
    if (width > 0.0f && stroke.alpha > 0)
    {
        SectorMesh edge;
        AppendSectorMesh(shape, kFeather, edge);
        AppendColoured(edge, stroke, opacity, mesh);
        shape.Inset = width;
    }
    if (fill.alpha > 0)
    {
        SectorMesh body;
        AppendSectorMesh(shape, kFeather, body);
        AppendColoured(body, fill, opacity, mesh);
    }
    Shape = render->MakeGeometry(std::move(mesh));
}
