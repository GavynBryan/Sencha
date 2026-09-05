#include <app/Game.h>
#include <app/GameModule.h>

//=============================================================================
// A game that does nothing.
//
// Every hook is the base class's empty one. The engine still boots, mounts the
// content roots it was configured with, loads whatever `+map` names, runs
// frames, and shuts down clean -- and this is the proof that it does so without
// a player, a camera, movement, networking, or any map policy being assumed.
//
// Start here when none of the other templates is your game. Add components in
// OnRegisterComponents, systems in OnRegisterSystems, and whatever your game
// decides a level means to it in a system that watches ZoneResidencyContext.
//=============================================================================
namespace
{
class BlankGame final : public Game
{
};
} // namespace

extern "C" SENCHA_GAME_EXPORT Game* SenchaCreateGameModule()
{
    static BlankGame instance;
    return &instance;
}

SENCHA_EXPORT_GAME_MODULE_ABI()
