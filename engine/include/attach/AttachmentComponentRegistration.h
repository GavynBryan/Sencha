#pragma once

#include <attach/AttachmentComponents.h>
#include <world/ComponentRegistrar.h>

// What a body wears. Runtime only and never replicated: each presenting machine
// attaches what its own presentation shows.
using AttachmentComponents = ComponentSet<AttachmentSet, Attachment>;

inline void RegisterAttachmentComponents(ComponentRegistrar& registrar)
{
    registrar.AddAll<AttachmentComponents>();
}
