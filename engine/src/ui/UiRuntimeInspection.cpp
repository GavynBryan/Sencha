#include "UiRuntime.h"

#include <RmlUi/Core.h>

#include <algorithm>
#include <string>

namespace
{
    UiElementBox BoxOf(Rml::Element& element, Rml::BoxArea area)
    {
        const Rml::Vector2f offset = element.GetAbsoluteOffset(area);
        const Rml::Vector2f size = element.GetBox().GetSize(area);
        return UiElementBox{ offset.x, offset.y, size.x, size.y };
    }

    Rml::BoxArea RmlAreaOf(UiBoxArea area)
    {
        switch (area)
        {
        case UiBoxArea::Margin:  return Rml::BoxArea::Margin;
        case UiBoxArea::Border:  return Rml::BoxArea::Border;
        case UiBoxArea::Padding: return Rml::BoxArea::Padding;
        case UiBoxArea::Content:
        default:                 return Rml::BoxArea::Content;
        }
    }
}

std::optional<UiScrollBox> UiRuntime::MeasureScroll(UiScreenHandle screen, std::string_view elementId) const
{
    const Screen* slot = ResolveScreen(screen);
    Rml::Element* element =
        slot != nullptr && slot->Document != nullptr ? slot->Document->GetElementById(std::string(elementId)) : nullptr;
    if (element == nullptr)
        return std::nullopt;
    return UiScrollBox{ .Left = element->GetScrollLeft(),
                        .Top = element->GetScrollTop(),
                        .ClientWidth = element->GetClientWidth(),
                        .ClientHeight = element->GetClientHeight(),
                        .ScrollWidth = element->GetScrollWidth(),
                        .ScrollHeight = element->GetScrollHeight() };
}

bool UiRuntime::ScrollIntoView(UiScreenHandle screen, std::string_view elementId)
{
    Screen* slot = ResolveScreen(screen);
    Rml::Element* element =
        slot != nullptr && slot->Document != nullptr ? slot->Document->GetElementById(std::string(elementId)) : nullptr;
    if (element == nullptr)
        return false;
    element->ScrollIntoView(Rml::ScrollIntoViewOptions(Rml::ScrollAlignment::Nearest, Rml::ScrollAlignment::Nearest,
                                                       Rml::ScrollBehavior::Instant, Rml::ScrollParentage::Closest));
    return true;
}

std::optional<UiElementBox> UiRuntime::MeasureElement(UiScreenHandle screen, std::string_view elementId,
                                                      UiBoxArea area) const
{
    const Screen* slot = ResolveScreen(screen);
    if (slot == nullptr || slot->Document == nullptr)
        return std::nullopt;

    Rml::Element* element = slot->Document->GetElementById(std::string(elementId));
    if (element == nullptr)
        return std::nullopt;
    return BoxOf(*element, RmlAreaOf(area));
}

namespace
{
    constexpr std::size_t kMaxElementRefsPerScreen = 8192;

    std::vector<std::string> SplitClasses(const Rml::String& names)
    {
        std::vector<std::string> out;
        std::size_t start = 0;
        while (start < names.size())
        {
            while (start < names.size() && names[start] == ' ')
                ++start;
            std::size_t end = start;
            while (end < names.size() && names[end] != ' ')
                ++end;
            if (end > start)
                out.emplace_back(names.substr(start, end - start));
            start = end;
        }
        return out;
    }
}

UiElementRef UiRuntime::MintElementRef(Screen& screen, UiScreenHandle handle, Rml::Element& element)
{
    std::size_t dead = screen.Elements.size();
    for (std::size_t i = 0; i < screen.Elements.size(); ++i)
    {
        Screen::ElementSlot& slot = screen.Elements[i];
        if (slot.Element && slot.Element.get() == &element)
            return UiElementRef{ handle, static_cast<std::uint32_t>(i), slot.Serial };
        if (!slot.Element && dead == screen.Elements.size())
            dead = i;
    }
    if (dead == screen.Elements.size())
    {
        if (screen.Elements.size() >= kMaxElementRefsPerScreen)
            return {};
        screen.Elements.emplace_back();
    }
    Screen::ElementSlot& slot = screen.Elements[dead];
    slot.Element = element.GetObserverPtr();
    slot.Serial = screen.NextElementSerial++;
    return UiElementRef{ handle, static_cast<std::uint32_t>(dead), slot.Serial };
}

Rml::Element* UiRuntime::ResolveElement(UiElementRef ref) const
{
    if (!ref.IsValid())
        return nullptr;
    const Screen* screen = ResolveScreen(ref.Screen);
    if (screen == nullptr || screen->Document == nullptr || ref.Slot >= screen->Elements.size())
        return nullptr;
    const Screen::ElementSlot& slot = screen->Elements[ref.Slot];
    if (slot.Serial != ref.Serial || !slot.Element)
        return nullptr;
    // Alive is not enough: the document engine detaches a removed element at
    // once and frees it a frame later, and "in the document" is the answer an
    // inspector is asking for. Walk up to the screen's document, or nothing.
    Rml::Element* element = slot.Element.get();
    for (Rml::Element* up = element; up != nullptr; up = up->GetParentNode())
    {
        if (up == static_cast<Rml::Element*>(screen->Document))
            return element;
    }
    return nullptr;
}

UiElementInfo UiRuntime::Describe(Screen& screen, UiScreenHandle handle, Rml::Element& element,
                                  UiElementRef ref)
{
    UiElementInfo info;
    info.Ref = ref;
    info.Tag = element.GetTagName();
    info.Id = element.GetId();
    info.Classes = SplitClasses(element.GetClassNames());
    for (const auto& [name, value] : element.GetAttributes())
        info.Attributes.emplace_back(name, value.Get<Rml::String>());
    info.Boxes.Margin = BoxOf(element, Rml::BoxArea::Margin);
    info.Boxes.Border = BoxOf(element, Rml::BoxArea::Border);
    info.Boxes.Padding = BoxOf(element, Rml::BoxArea::Padding);
    info.Boxes.Content = BoxOf(element, Rml::BoxArea::Content);

    // The document is the root of what a screen describes; its own parent is
    // the context, which no screen owns.
    if (&element != static_cast<Rml::Element*>(screen.Document))
    {
        std::uint32_t depth = 0;
        for (Rml::Element* up = element.GetParentNode();
             up != nullptr && up != static_cast<Rml::Element*>(screen.Document);
             up = up->GetParentNode())
            ++depth;
        info.Depth = depth + 1;
        if (Rml::Element* parent = element.GetParentNode())
            info.Parent = MintElementRef(screen, handle, *parent);
    }
    return info;
}

UiElementRef UiRuntime::ElementAt(UiSurfaceId surface, Vec2d surfacePoint)
{
    Surface* slot = ResolveSurface(surface);
    if (slot == nullptr || slot->Context == nullptr)
        return {};
    Rml::Element* element =
        slot->Context->GetElementAtPoint(Rml::Vector2f(surfacePoint.X, surfacePoint.Y));
    if (element == nullptr)
        return {};
    Rml::ElementDocument* owner = element->GetOwnerDocument();
    for (Screen& screen : Screens)
    {
        if (screen.Live && screen.Document != nullptr && screen.Document == owner)
            return MintElementRef(screen, HandleOf(screen), *element);
    }
    return {};
}

std::optional<UiElementInfo> UiRuntime::DescribeElement(UiElementRef ref)
{
    Rml::Element* element = ResolveElement(ref);
    Screen* screen = ResolveScreen(ref.Screen);
    if (element == nullptr || screen == nullptr)
        return std::nullopt;
    return Describe(*screen, ref.Screen, *element, ref);
}

std::vector<UiElementRef> UiRuntime::ElementChildren(UiElementRef ref)
{
    std::vector<UiElementRef> out;
    Rml::Element* element = ResolveElement(ref);
    Screen* screen = ResolveScreen(ref.Screen);
    if (element == nullptr || screen == nullptr)
        return out;
    const int count = element->GetNumChildren(false);
    out.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i)
        if (Rml::Element* child = element->GetChild(i))
            out.push_back(MintElementRef(*screen, ref.Screen, *child));
    return out;
}

std::vector<UiElementInfo> UiRuntime::ElementTree(UiScreenHandle handle)
{
    std::vector<UiElementInfo> out;
    Screen* screen = ResolveScreen(handle);
    if (screen == nullptr || screen->Document == nullptr)
        return out;

    // Preorder, iteratively: a document is wide rather than deep, but a
    // recursion bounded by authored nesting is still a recursion bounded by
    // somebody else.
    std::vector<Rml::Element*> stack{ screen->Document };
    while (!stack.empty())
    {
        Rml::Element* element = stack.back();
        stack.pop_back();
        const UiElementRef ref = MintElementRef(*screen, handle, *element);
        if (!ref.IsValid())
            break;
        out.push_back(Describe(*screen, handle, *element, ref));
        const int count = element->GetNumChildren(false);
        for (int i = count - 1; i >= 0; --i)
            if (Rml::Element* child = element->GetChild(i))
                stack.push_back(child);
    }
    return out;
}

std::optional<std::string> UiRuntime::ComputedProperty(UiElementRef ref,
                                                       std::string_view property) const
{
    Rml::Element* element = ResolveElement(ref);
    if (element == nullptr)
        return std::nullopt;
    const Rml::Property* value = element->GetProperty(Rml::String(property));
    if (value == nullptr)
        return std::nullopt;
    return std::string(value->ToString());
}

std::uint32_t UiRuntime::LiveGeometryCount() const
{
    return Recorder != nullptr ? Recorder->LiveGeometryCount() : 0;
}

std::uint32_t UiRuntime::LiveTextureCount() const
{
    return Recorder != nullptr ? Recorder->LiveTextureCount() : 0;
}
