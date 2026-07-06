#include <script/ScriptCallbacks.h>

#include <cstddef>

std::int32_t FindScriptCallback(const ScriptModule& module, const ScriptDeclaration& decl,
                                std::string_view name)
{
    for (const ScriptCallbackDef& callback : decl.Callbacks)
    {
        if (module.GetString(callback.Name) == name)
        {
            return static_cast<std::int32_t>(callback.FunctionIndex);
        }
    }
    return -1;
}

std::int32_t FindScriptFixedCallback(const ScriptModule& module, const ScriptDeclaration& decl,
                                     std::int32_t state)
{
    if (state >= 0 && static_cast<std::size_t>(state) < decl.States.size())
    {
        const std::string_view stateName = module.GetString(decl.States[state]);
        constexpr std::string_view suffix = ".fixed";
        // Match "<stateName>.fixed" without allocating a joined string each
        // tick, since this runs per entity on the fixed loop.
        for (const ScriptCallbackDef& callback : decl.Callbacks)
        {
            const std::string_view name = module.GetString(callback.Name);
            if (name.size() == stateName.size() + suffix.size()
                && name.substr(0, stateName.size()) == stateName
                && name.substr(stateName.size()) == suffix)
            {
                return static_cast<std::int32_t>(callback.FunctionIndex);
            }
        }
    }
    return FindScriptCallback(module, decl, "fixed");
}
