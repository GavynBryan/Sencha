#include <core/console/CVarRead.h>

#include <core/console/ConsoleRegistry.h>

#include <variant>

namespace
{
const CVarValue* FindValue(const ConsoleRegistry* console, std::string_view name)
{
    if (console == nullptr)
        return nullptr;
    const CVarMetadata* metadata = console->FindCVar(name);
    return metadata != nullptr ? &metadata->CurrentValue : nullptr;
}
} // namespace

double ReadCVarDouble(const ConsoleRegistry* console,
                      std::string_view name,
                      double fallback)
{
    const CVarValue* value = FindValue(console, name);
    if (value == nullptr)
        return fallback;
    const double* number = std::get_if<double>(value);
    return number != nullptr ? *number : fallback;
}

float ReadCVarFloat(const ConsoleRegistry* console,
                    std::string_view name,
                    float fallback)
{
    return static_cast<float>(
        ReadCVarDouble(console, name, static_cast<double>(fallback)));
}

bool ReadCVarBool(const ConsoleRegistry* console,
                  std::string_view name,
                  bool fallback)
{
    const CVarValue* value = FindValue(console, name);
    if (value == nullptr)
        return fallback;
    const bool* flag = std::get_if<bool>(value);
    return flag != nullptr ? *flag : fallback;
}

std::string ReadCVarString(const ConsoleRegistry* console,
                           std::string_view name,
                           std::string_view fallback)
{
    const CVarValue* value = FindValue(console, name);
    if (value == nullptr)
        return std::string(fallback);
    const std::string* text = std::get_if<std::string>(value);
    return text != nullptr ? *text : std::string(fallback);
}
