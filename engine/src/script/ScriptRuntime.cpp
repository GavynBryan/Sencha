#include <script/ScriptRuntime.h>

#include <ecs/World.h>

void RegisterScriptRuntime(World& world, std::uint64_t worldSeed)
{
    if (!world.IsRegistered<ScriptCueBuffer>())
    {
        world.RegisterComponent<ScriptCueBuffer>();
    }
    if (!world.HasResource<ScriptRuntime>())
    {
        world.AddResource<ScriptRuntime>().WorldSeed = worldSeed;
    }
    else
    {
        world.GetResource<ScriptRuntime>().WorldSeed = worldSeed;
    }
}
