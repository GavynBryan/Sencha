#include <camera/CameraEffectArguments.h>

#include <time/SimClock.h>

DataFieldSchema CameraEffectSubjectField()
{
    DataFieldSchema subject = MakeDataField(DataFieldKind::Entity, "subject", "Subject",
                                            "Only the view this entity owns presents it; every view when unset.");
    subject.Required = false;
    return subject;
}

std::uint64_t CameraEffectTickOf(const VerbInvocation& invocation, const FixedSimulationLoop& clock)
{
    return invocation.Tick != 0 ? invocation.Tick : clock.GetTickIndex();
}
