#include <assets/script/ScriptCache.h>

#include <utility>

ScriptCache::ScriptCache(LoggingProvider& logging)
    : Log(logging.GetLogger<ScriptCache>())
{
    ReserveNullSlot();
}

ScriptCache::~ScriptCache()
{
    FreeAllEntries();
}

ScriptHandle ScriptCache::Register(std::string_view path,
                                   std::shared_ptr<const ScriptModule> module)
{
    if (module == nullptr)
    {
        Log.Error("ScriptCache: refused to register null module for '{}'", path);
        return {};
    }
    ScriptEntry entry;
    entry.SchemaHash = ComputeScriptComponentSchemaHash(*module);
    entry.Module = std::move(module);
    return AllocNamedHandle(path, std::move(entry));
}

ScriptHandle ScriptCache::Find(std::string_view path) const
{
    return FindRegisteredHandle(path);
}

std::string_view ScriptCache::GetName(ScriptHandle handle) const
{
    return GetRegisteredPath(handle);
}

std::shared_ptr<const ScriptModule> ScriptCache::Get(ScriptHandle handle) const
{
    const ScriptEntry* entry = Resolve(handle);
    return entry ? entry->Module : nullptr;
}

ScriptReloadResult ScriptCache::ReloadInPlace(std::string_view path,
                                              std::shared_ptr<const ScriptModule> module)
{
    ScriptEntry* entry = Resolve(FindRegisteredHandle(path));
    if (entry == nullptr)
    {
        return ScriptReloadResult::NotResident;
    }
    const uint64_t newHash = ComputeScriptComponentSchemaHash(*module);
    if (newHash != entry->SchemaHash)
    {
        // A layout change cannot mutate a live World's component storage;
        // the caller reloads the world instead (spec answer 17). The old
        // module stays resident until then.
        return ScriptReloadResult::LayoutChanged;
    }
    entry->Module = std::move(module);
    return ScriptReloadResult::Swapped;
}

bool ScriptCache::OnLoad(std::string_view, ScriptEntry&)
{
    // Modules arrive validated through Register / the loader's commit; there
    // is no path-driven load (Decision I: caches perform no file IO).
    return false;
}

void ScriptCache::OnFree(ScriptEntry& entry)
{
    entry.Module.reset();
    entry.SchemaHash = 0;
}

bool ScriptCache::IsEntryLive(const ScriptEntry& entry) const
{
    return entry.Module != nullptr;
}
