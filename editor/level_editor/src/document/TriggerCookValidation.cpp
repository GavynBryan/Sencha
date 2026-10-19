#include "TriggerCookValidation.h"

#include "EditorDocument.h"

#include <assets/runtime/RuntimeAssets.h>
#include <authored/WorldVocabulary.h>
#include <core/identity/Id.h>
#include <logic/TriggerPrograms.h>
#include <world/identity/PersistentIdComponent.h>
#include <world/registry/Registry.h>

#include <format>

bool ValidateTriggerProgramsForCook(
    const EditorDocument& document, RuntimeAssets* assets, std::vector<std::string>& errors)
{
    const World& world = document.GetRegistry().Components;
    if (!world.IsRegistered<Trigger>())
        return true;
    const auto before = errors.size();
    auto environment = MakeVerbBindingEnvironment(world);
    if (assets != nullptr)
    {
        environment.Assets = &assets->Registry;
        environment.DataAssets = &assets->DataAssets;
    }
    world.ForEachComponent<Trigger>([&](EntityId entity, const Trigger& trigger)
    {
        std::vector<std::string> located;
        std::vector<AssetRef> dependencies;
        std::vector<AssetLease> leases;
        const JsonValue* source = TriggerProgramSource(world, entity);
        (void)ValidateTriggerActivation(trigger, located);
        if (source == nullptr)
            located.emplace_back("trigger program: missing inline program ownership");
        else if (CollectTriggerProgramDependencies(*source, dependencies, located))
        {
            for (const AssetRef& dependency : dependencies)
            {
                if (assets == nullptr)
                {
                    located.push_back("trigger asset reference: asset environment is required for " + dependency.Path);
                    continue;
                }
                if (dependency.Type != AssetType::Data)
                    continue;
                auto lease = assets->Assets.LoadLease(dependency.Path, AssetType::Data);
                if (!lease.IsValid())
                    located.push_back("trigger data reference: could not load " + dependency.Path);
                else
                    leases.push_back(std::move(lease));
            }
            TriggerPrograms::Program program;
            (void)CompileWorldTriggerProgram(*source, world, environment, program, located);
        }
        const auto* identity = world.TryGet<PersistentIdComponent>(entity);
        const std::string owner = identity != nullptr
            ? PersistentEntityIdToString(identity->Id)
            : std::format("{}:{}", entity.Index, entity.Generation);
        for (const auto& message : located)
            errors.push_back(std::format("entity {}: {}", owner, message));
    });
    return errors.size() == before;
}
