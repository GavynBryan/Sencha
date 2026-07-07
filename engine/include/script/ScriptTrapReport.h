#pragma once

#include <cstdint>

struct ScriptModule;
struct ScriptDeclaration;
struct ScriptInvokeResult;
class LoggingProvider;

// Logs one structured diagnostic for a trapped script callback:
//   script <file>: <declaration>.<function>:<line>: trap <CODE> at tick <N>
// resolving the source line from the module's line table. No-op when the result
// did not trap or no logger is available. This is the only place a script
// runtime error becomes visible; under PIE it reaches the editor through the
// player's log file.
void ReportScriptTrap(LoggingProvider& logging, const ScriptModule& module,
                      const ScriptDeclaration& decl, const ScriptInvokeResult& result,
                      std::uint64_t tick);
