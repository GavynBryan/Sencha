#pragma once

#include <ecs/ComponentTypeId.h>

#include <vector>

class Game;

// A game module's components lent to the editor while this is held: its
// serializers in the editor's registry, so scenes holding them load and edit,
// and its vocabulary installed into every document's World. The editor never
// runs the game's lifecycle. Taken back when this goes -- after the documents,
// whose worlds hold what the module compiled, and before the module is unmapped.
class ModuleComponents
{
public:
    explicit ModuleComponents(Game* module);
    ~ModuleComponents();

    ModuleComponents(const ModuleComponents&) = delete;
    ModuleComponents& operator=(const ModuleComponents&) = delete;

private:
    std::vector<ComponentTypeId> Added;
};
