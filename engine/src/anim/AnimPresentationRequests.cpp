#include <anim/AnimPresentationRequests.h>

#include <anim/AnimRigBinding.h>

const AnimRequestSet* AnimLayerRequests(const AnimBoundRig& rig, std::size_t layer, const AnimRequestSet* simulation,
                                        const AnimPresentationRequests* presentation)
{
    if (!rig.Layers[layer].Presentation)
        return simulation;
    return presentation != nullptr ? &presentation->Set : nullptr;
}
