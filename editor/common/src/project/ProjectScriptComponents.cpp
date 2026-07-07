#include "project/ProjectScriptComponents.h"

#include <core/assets/AssetRef.h>
#include <core/assets/AssetRegistry.h>
#include <core/logging/Logger.h>
#include <core/logging/LoggingProvider.h>
#include <script/ScriptComponentSerializer.h>
#include <script/ScriptModule.h>

#include <cstddef>
#include <fstream>
#include <vector>

namespace
{
    // Logging category tag (GetLogger keys on typeid, so it must be complete).
    struct ScriptComponentRegistration
    {
    };

    bool ReadFileBytes(const std::string& path, std::vector<std::byte>& out)
    {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file)
            return false;
        const std::streamsize size = file.tellg();
        if (size < 0)
            return false;
        out.resize(static_cast<std::size_t>(size));
        file.seekg(0);
        return static_cast<bool>(file.read(reinterpret_cast<char*>(out.data()), size));
    }
}

void RegisterProjectScriptComponents(const AssetRegistry& registry, LoggingProvider& logging)
{
    Logger& log = logging.GetLogger<ScriptComponentRegistration>();

    for (const auto& [path, record] : registry.Records())
    {
        if (record.Type != AssetType::Script)
            continue;

        std::vector<std::byte> bytes;
        if (!ReadFileBytes(record.FilePath, bytes))
        {
            log.Warn("could not read cooked script '{}' ({})", path, record.FilePath);
            continue;
        }

        ScriptModuleParseResult parsed = ParseScriptModule(bytes);
        if (!parsed.Ok)
        {
            log.Warn("cooked script '{}' failed to parse: {}", path, parsed.Error);
            continue;
        }

        RegisterScriptComponentSerializers(parsed.Module);
    }
}
