#include "RmlWheelZoom.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Event.h>

#include <string>

void RmlWheelZoom::ProcessEvent(Rml::Event& event)
{
    if (event.GetId() != Rml::EventId::Mousescroll || event.GetParameter<int>("ctrl_key", 0) == 0)
        return;
    const Rml::String attribute(kAttribute);
    for (Rml::Element* element = event.GetTargetElement(); element != nullptr; element = element->GetParentNode())
    {
        if (element->HasAttribute(attribute))
        {
            event.StopPropagation();
            return;
        }
    }
}
