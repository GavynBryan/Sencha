#include <camera/CameraEffectArguments.h>

#include <authored/AuthoredValue.h>
#include <time/SimClock.h>

#include <utility>

DataFieldSchema CameraEffectSubjectField()
{
    DataFieldSchema subject = MakeDataField(DataFieldKind::Entity, "subject", "Subject",
                                            "Only the view this entity owns presents it; every view when unset.");
    subject.Required = false;
    return subject;
}

DataFieldSchema CameraEffectSecondsField(std::string key, std::string label, std::string summary)
{
    DataFieldSchema field = MakeDataField(DataFieldKind::Float, std::move(key), std::move(label), std::move(summary));
    field.Units = "s";
    field.Required = false;
    field.Default = 0.0;
    field.Numeric.Minimum = 0.0;
    return field;
}

bool ReadCameraEffectSeconds(const AuthoredArguments& arguments, std::size_t slot, float& out)
{
    double seconds = 0.0;
    if (!arguments.TryGetFloat(slot, seconds))
        return false;
    out = static_cast<float>(seconds);
    return true;
}

std::uint64_t CameraEffectTickOf(const VerbInvocation& invocation, const FixedSimulationLoop& clock)
{
    return invocation.Tick != 0 ? invocation.Tick : clock.GetTickIndex();
}
