#pragma once

#include <participant/ParticipantControl.h>
#include <world/ComponentRegistrar.h>

inline void RegisterParticipantComponents(ComponentRegistrar& registrar)
{
    registrar.Add<ParticipantControl>();
    registrar.Add<LocalParticipant>();
}

