#include "TriggerVolumeRecipe.h"

#include <logic/TriggerProgram.h>

EntitySnapshot TriggerVolumeRecipe::Build(const EditorCreateContext&) const
{
    EntitySnapshot snapshot;
    snapshot.Components = JsonValue::Object{
        { "volume", JsonValue::Object{} },
        { "trigger", JsonValue::Object{ { "program", EmptyTriggerProgram() } } },
    };
    return snapshot;
}
