#include <authored/WorldVocabulary.h>
#include <core/assets/AssetRegistry.h>
#include <ecs/World.h>
#include <format>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <logic/TriggerPrograms.h>
#include <utility>

TriggerProgramHandle TriggerPrograms::Attach(EntityId owner, TriggerProgramHandle copy)
{
    if (const auto found = Owners.find(owner); found != Owners.end())
        return found->second;
    const auto *original = Entries.Find(copy);
    const auto handle = Entries.Emplace();
    auto &entry = *Entries.Find(handle);
    entry.Owner = owner;
    Owners.emplace(owner, handle);
    Dirty = true;
    if (original != nullptr)
        entry.Source = original->Source;
    return handle;
}
void TriggerPrograms::Detach(EntityId owner)
{
    const auto found = Owners.find(owner);
    if (found == Owners.end())
        return;
    (void)Entries.Erase(found->second);
    Owners.erase(found);
}
const JsonValue *TriggerPrograms::Source(EntityId owner, TriggerProgramHandle handle) const
{
    const auto *entry = Entries.Find(handle);
    return entry != nullptr && entry->Owner == owner ? &entry->Source : nullptr;
}
bool TriggerPrograms::Set(EntityId owner, TriggerProgramHandle handle, JsonValue source)
{
    auto *entry = Entries.Find(handle);
    if (entry == nullptr || entry->Owner != owner)
        return false;
    entry->Source = std::move(source);
    entry->Built = false;
    Dirty = true;
    return true;
}
std::shared_ptr<const TriggerPrograms::Program> TriggerPrograms::Resolve(EntityId owner, TriggerProgramHandle handle,
                                                                         const World &world,
                                                                         const VerbBindingEnvironment &environment,
                                                                         std::vector<std::string> &newErrors)
{
    auto *entry = Entries.Find(handle);
    if (entry == nullptr || entry->Owner != owner)
        return nullptr;
    const auto stamp = Stamp::Of(world, environment);
    if (entry->Built && entry->BuiltAgainst == stamp)
        return entry->Compiled;
    entry->Built = true;
    entry->BuiltAgainst = stamp;
    entry->Compiled.reset();
    ++Rebuilds;
    auto program = std::make_shared<Program>();
    std::vector<std::string> errors;
    const bool ok = CompileWorldTriggerProgram(entry->Source, world, environment, *program, errors);
    for (auto &error : errors)
        newErrors.push_back(std::format("trigger {}:{}: {}", owner.Index, owner.Generation, error));
    if (ok)
        entry->Compiled = std::move(program);
    return entry->Compiled;
}
bool CompileWorldTriggerProgram(const JsonValue &source, const World &world,
                                const VerbBindingEnvironment &environment, TriggerPrograms::Program &out,
                                std::vector<std::string> &errors)
{
    TriggerPrograms::Program program;
    if (!CompileTriggerProgram(source, environment, FindAuthoredQueryRegistry(world), program, errors))
        return false;
    bool ok = true;
    for (std::size_t i = 0; i < program.RequiredComponents.size(); ++i)
    {
        const auto id = world.GetComponentIdByType(program.RequiredComponents[i]);
        if (id == InvalidComponentId)
        {
            errors.push_back(std::format("trigger require_components[{}]: unregistered component '{}'", i,
                                         source.Find("require_components")->AsArray()[i].AsString()));
            ok = false;
        }
        else
            program.Eligibility.push_back(id);
    }
    if (ok)
        out = std::move(program);
    return ok;
}
TriggerPrograms::Stamp TriggerPrograms::Stamp::Of(const World &world, const VerbBindingEnvironment &environment)
{
    const auto *queries = FindAuthoredQueryRegistry(world);
    Stamp stamp;
    stamp.Verbs = environment.Verbs != nullptr ? CatalogStamp::Of(*environment.Verbs) : CatalogStamp{};
    stamp.Queries = queries != nullptr ? CatalogStamp::Of(*queries) : CatalogStamp{};
    stamp.Tags = environment.Tags != nullptr ? TagVocabularyStamp::Of(*environment.Tags).Count : 0;
    stamp.Assets = environment.Assets;
    stamp.AssetRevision = environment.Assets != nullptr ? environment.Assets->Revision() : 0;
    stamp.ComponentCount = world.RegisteredComponentCount();
    return stamp;
}
void TriggerPrograms::Prepare(const World &world, const VerbBindingEnvironment &environment,
                              std::vector<std::string> &newErrors)
{
    const auto stamp = Stamp::Of(world, environment);
    if (Prepared && !Dirty && PreparedAgainst == stamp)
        return;
    Entries.ForEach([&](TriggerProgramHandle handle, const Entry &entry) {
        (void)Resolve(entry.Owner, handle, world, environment, newErrors);
    });
    PreparedAgainst = stamp;
    Prepared = true;
    Dirty = false;
}
TriggerPrograms &TriggerProgramsOf(World &world)
{
    auto *programs = world.TryGetResource<TriggerPrograms>();
    return programs != nullptr ? *programs : world.AddResource<TriggerPrograms>();
}
bool SetTriggerProgram(World &world, EntityId trigger, JsonValue source)
{
    const auto *component = std::as_const(world).TryGet<Trigger>(trigger);
    return component != nullptr && TriggerProgramsOf(world).Set(trigger, component->Program, std::move(source));
}
const JsonValue *TriggerProgramSource(const World &world, EntityId trigger)
{
    const auto *component = world.TryGet<Trigger>(trigger);
    const auto *programs = world.TryGetResource<TriggerPrograms>();
    return component != nullptr && programs != nullptr ? programs->Source(trigger, component->Program) : nullptr;
}
