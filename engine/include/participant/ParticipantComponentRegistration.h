#pragma once

#include <participant/ParticipantControl.h>
#include <world/ComponentRegistrar.h>

// Who is taking part, and which one of them this machine presents.
using ParticipantComponents = ComponentSet<ParticipantControl, LocalParticipant>;

inline void RegisterParticipantComponents(ComponentRegistrar& registrar)
{
    registrar.AddAll<ParticipantComponents>();
}

