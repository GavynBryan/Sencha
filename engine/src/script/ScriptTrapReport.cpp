#include <script/ScriptTrapReport.h>

#include <core/logging/Logger.h>
#include <core/logging/LoggingProvider.h>
#include <script/ScriptModule.h>
#include <script/ScriptTrap.h>
#include <script/ScriptVm.h>

#include <string_view>

namespace
{
    // Logging category tag (GetLogger keys on typeid, so it must be complete).
    struct ScriptTrap
    {
    };

    // The source line of a module-absolute code offset: the last line-table entry
    // that starts at or before it. Zero when there is no line table (a shipping
    // build strips it).
    std::uint32_t ResolveLine(const ScriptModule& module, std::uint32_t absoluteOffset)
    {
        std::uint32_t line = 0;
        bool found = false;
        std::uint32_t bestOffset = 0;
        for (const ScriptLineEntry& entry : module.LineTable)
        {
            if (entry.CodeOffset <= absoluteOffset && (!found || entry.CodeOffset >= bestOffset))
            {
                bestOffset = entry.CodeOffset;
                line = entry.Line;
                found = true;
            }
        }
        return line;
    }
}

void ReportScriptTrap(LoggingProvider& logging, const ScriptModule& module,
                      const ScriptDeclaration& decl, const ScriptInvokeResult& result,
                      std::uint64_t tick)
{
    if (result.Ok())
    {
        return;
    }

    std::string_view function = "<fn>";
    std::uint32_t line = 0;
    if (result.TrapFunction < module.Functions.size())
    {
        const ScriptFunctionDef& fn = module.Functions[result.TrapFunction];
        function = module.GetString(fn.Name);
        line = ResolveLine(module, fn.CodeOffset + result.TrapCodeOffset);
    }
    const std::string_view file =
        module.DebugFiles.empty() ? std::string_view("<script>")
                                  : module.GetString(module.DebugFiles[0]);

    logging.GetLogger<ScriptTrap>().Error("script {}: {}.{}:{}: trap {} at tick {}", file,
                                          module.GetString(decl.Name), function, line,
                                          ScriptTrapName(result.Trap), tick);
}
