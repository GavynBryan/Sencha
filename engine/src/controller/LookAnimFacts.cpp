#include <controller/LookAnimFacts.h>

#include <anim/AnimFactProviders.h>
#include <controller/LookOrientation.h>

bool BindLookAnimFacts(AnimFactProviders& providers)
{
    bool bound = providers.BindField<&LookOrientation::Yaw>("LookYaw");
    bound = providers.BindField<&LookOrientation::Pitch>("LookPitch") && bound;
    return bound;
}
