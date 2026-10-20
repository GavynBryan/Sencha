#pragma once

#include <authored/BindingDependencyStamp.h>
#include <core/handle/HandlePool.h>
#include <ecs/ComponentId.h>
#include <ecs/EntityId.h>
#include <logic/TriggerProgram.h>
#include <memory>
#include <unordered_map>

class World;
class TriggerPrograms
{
  public:
    struct Program : CompiledTriggerProgram
    {
        std::vector<ComponentId> Eligibility;
    };
    TriggerProgramHandle Attach(EntityId owner, TriggerProgramHandle copy = {});
    void Detach(EntityId owner);
    [[nodiscard]] const JsonValue *Source(EntityId owner, TriggerProgramHandle handle) const;
    [[nodiscard]] bool Set(EntityId owner, TriggerProgramHandle handle, JsonValue source);
    [[nodiscard]] std::shared_ptr<const Program> Resolve(EntityId owner, TriggerProgramHandle handle,
                                                         const World &world, const VerbBindingEnvironment &environment,
                                                         std::vector<std::string> &newErrors);
    void Prepare(const World &world, const VerbBindingEnvironment &environment, std::vector<std::string> &newErrors);
    [[nodiscard]] std::size_t Size() const
    {
        return Entries.Size();
    }
    [[nodiscard]] std::uint64_t RebuildCount() const
    {
        return Rebuilds;
    }

  private:
    struct Stamp
    {
        CatalogStamp Verbs;
        CatalogStamp Queries;
        std::size_t Tags = 0;
        const AssetRegistry *Assets = nullptr;
        std::uint64_t AssetRevision = 0;
        std::size_t ComponentCount = 0;
        bool operator==(const Stamp &) const = default;
        static Stamp Of(const World &world, const VerbBindingEnvironment &environment);
    };
    struct Entry
    {
        EntityId Owner;
        JsonValue Source = EmptyTriggerProgram();
        std::shared_ptr<const Program> Compiled;
        Stamp BuiltAgainst;
        bool Built = false;
    };
    HandlePool<TriggerProgramTag, Entry> Entries;
    std::unordered_map<EntityId, TriggerProgramHandle, EntityIdHash> Owners;
    std::uint64_t Rebuilds = 0;
    Stamp PreparedAgainst;
    bool Dirty = true;
    bool Prepared = false;
};

[[nodiscard]] TriggerPrograms &TriggerProgramsOf(World &world);
[[nodiscard]] bool SetTriggerProgram(World &world, EntityId trigger, JsonValue source);
[[nodiscard]] const JsonValue *TriggerProgramSource(const World &world, EntityId trigger);
[[nodiscard]] bool CompileWorldTriggerProgram(const JsonValue &source, const World &world,
                                              const VerbBindingEnvironment &environment,
                                              TriggerPrograms::Program &out, std::vector<std::string> &errors);
