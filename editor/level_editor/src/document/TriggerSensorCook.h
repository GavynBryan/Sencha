#pragma once

#include <ecs/EntityId.h>
#include <physics/components/Collider.h>

#include <string>
#include <vector>

class EditorScene;

struct CookTriggerSensor
{
    EntityId Entity;
    Collider Sensor;
};

[[nodiscard]] bool CollectTriggerSensors(const EditorScene& scene, std::vector<CookTriggerSensor>& sensors,
                                         std::vector<std::string>& errors);
